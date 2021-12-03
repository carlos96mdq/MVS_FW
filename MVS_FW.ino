//************ DESCRIPCIÓN ************//

/* Código que permite elegir que modo de operación ejecutar:
 * Modo 1: Modulación
 * Se Tx el pulso modulado cada 100mseg para poder observar las señales en las distintas etapas del Rx

 * Modo 2: Mediciones
 * Se envía una portadora de 40KHz y se activa una interrupción si el Rx recibe la portadora o si se alcanza el TIME OUT
 * Se agrega un tiempo limite de Tx, para que se envíe una cantidad fija de ciclos de portadora (modulación)
 * Se modificaron las variables que se utilizan dentro de la ISR para que sean volatiles 
 * Se desactivo el digitalWrite dentro de la interrupción
 * Se cambio como se trabaja con los contadores
 * Ahora la interrupcción de señal recibida comienza a observarse en el momento que se deja de transmitir
 * Se modificó el loop para realizar una cantidad determinada de mediciones en vez de solo una
 * Se cambio la interrupción de un FALLING a un LOW, por temas que no funcionaba muy bien con FALLING
 * Compatible con Tx v8 y Rx v8
 * Sistema que une: Medición en Agua + Pantalla LCD + Tarjeta SD
 * Se modificó el guardado SD para que solo almacene todos los valores tomados, sin palabras ni valores promedios
 * 
 * El código está implementado en base al "Prueba_modulacion_v4.ino" y "Sistema_Completo_v3.ino"
 */

//************ ALGORITMO ************//

/* Se inicializa la comunicación serie
 * Se inicializan los pines 
 * Se selecciona que modo de operación ejecutar
 * 
 * Modo 1: Modulación
 * 
 * Modo 2: mediciones
 * Se reinicia el Timer, para este caso se utiliza el Timer1 que es de 16 bits y trabaja a una frecuencia de 16 MHz, con unar resolución de 62.5ns
 * Se inicializa el Timer1
 * Se comienza a transmitir
 * Se inicializa la interrupción de Rx
 * Se espera que la señal llegue o salte el time out
 * Se calcula el tiempo de transmisión
 * Se repite la medición un numero entero de veces
 * Se calcula el promedio de todas las mediciones
 * Se muestra en pantalla los resultados
 * Se acaba el programa
 */
 
//************ INCLUDES ************//

#include <LiquidCrystal_I2C.h>  // Libreria para el manejo de la pantalla por I2C
#include <SPI.h>                // Libreria para el manejo de comunciaciones SPI (tarjeta SD)
#include <SD.h>                 // Libreria para el manejo de la tarjeta SD


//************ DEFINICIONES ************//

#define SSpin 10  // Pin SS del módulo de la tarjeta SD
#define CS 9      // Señal de salida que controla la modulación mediante la NAND
#define RX 3      // Señal proveniente de la etapa final del Rx
#define ARRIBA 7  
#define ABAJO 6   // Botones  
#define ENTER 5       

//************ CONSTANTES ************//

const unsigned long timeOut = 65000;                                  // Después de este tiempo se tomará como que no hubo eco (está en ticks)
const unsigned long transmitionTime = 3200;                           // Tiempo en useg que se transmite (está en ticks) 3200 ticks = 200useg
const int maxMeasureQuantity = 50;                                    // Indica la cantidad máxima que acepta el programa por el ancho del array creado
const int measureValues[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};  // Almacena los valores válidos de cantidad de muestras a elegir que se muestra en pantalla
LiquidCrystal_I2C lcd(0x27,20,4);                                     // Se crea objeto de la pantalla LCD

//************ VARIABLES GLOBALES ************//

volatile bool received = false;               // Indica si el eco ya fue recibido 
bool transmited = false;                      // Indica si ya se terminó de transmitir
volatile unsigned long overflowCount = 0;     // Se encarga de aumentar en +1 cada vez que el contador del Timer1 llega a un overflow 
unsigned long startTime = 0;                  // El momento en el que se comenzó a transmitir
volatile unsigned long finishTime = 0;        // El momento en que llegó la señal 
bool chosed = false;                          // Indica si ya se eligió la cantidad de muestras o el modo
int chosedValue = 0;                          // Lugar de vector measureValues[]
int modValue = 1;                             // Modo de operación (1:Modulación 2:Medición)
File archivo;                                 // Creo objeto del tipo File para la escritura de archivos

//************ DECLARACION FUNCIONES ************//

void initTimer1();
void initInterrupt();
void timeOutReached();
void eco();

//************ SETUP ************// 
 
void setup() {

  //************ INICIALIZACION LCD ************//
  
  lcd.init();               // Inicializo pantalla
  lcd.backlight();          // Inicializo brillo de la pantalla (aunque al parecer el controlador I2C no funciona, así que se maneja por HW)
  lcd.setCursor(8,0);       // Ubico el cursor en la pantalla LCD para escribir
  lcd.print("MVS");         // Escribo en pantalla LCD
  delay(2000);
  
  //************ INICIALIZACION SD ************//
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Inicializando SD..."); // Inicializo módulo SD
  lcd.setCursor(0,2);
  if (!SD.begin(SSpin)) {
    lcd.print("Fallo en SD");
    delay(2000);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Reinicie programa");
    while(true); // Si falla la inicialización se termina el programa
  }
  lcd.print("SD OK");
  delay(2000);
  
  //************ PINES ************//
  
  pinMode(CS, OUTPUT);            // Pin para el control de Tx
  pinMode(RX, INPUT);             // Pin para recibir la interrupción de llegada de señal
  digitalWrite(CS, LOW);          // Desahabilito Tx
  pinMode(ARRIBA, INPUT_PULLUP);  // Botones
  pinMode(ABAJO, INPUT_PULLUP);
  pinMode(ENTER, INPUT_PULLUP); 

}

void loop() {
  
  //************ INDICO MODO DE OPERACION ************//
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Modo de operacion:");
  lcd.setCursor(0,1);
  lcd.print("1: Modulacion");
  lcd.setCursor(0,2);
  lcd.print("2: Mediciones");
  lcd.setCursor(10,3);
  lcd.print(modValue);
  
  while (!chosed){
    if (digitalRead(ARRIBA) == LOW || digitalRead(ABAJO) == LOW){
      if(modValue == 1)
        modValue = 2;
      else
        modValue = 1;
      lcd.setCursor(10,3);
      lcd.print(modValue);
      delay(250);
    }
    else if (digitalRead(ENTER) == LOW){
      chosed = true;
      lcd.clear();
    }
  }
  
  switch(modValue) {
    
      //************ MODULACION ************//
      
      case 1:
        lcd.setCursor(5,2);
        lcd.print("Modulacion");
        while(true){              // Se habilita la Tx durante un tiempo de 200useg, a intervalos de 100mseg
          digitalWrite(CS, HIGH);
          delayMicroseconds(200);
          digitalWrite(CS, LOW);
          delay(100);
        }
      break;

      //************ MEDICION ************//
      
      case 2:
      
        int measureQuantity = 0;                      // Indica la cantidad de mediciones a realizar
        double measureAverage = 0;                    // Indica el promedio en las mediciones 
        double measureTime[maxMeasureQuantity] = {};  // Arreglo que engloba a todas las mediciones realizadas

        
        lcd.setCursor(5,2);
        lcd.print("Mediciones");
        delay(2000);
        
        //************ INDICO CANTIDAD DE MUESTRAS ************//
        MEDITION:
        chosed = false;
        lcd.clear();
        lcd.setCursor(0,0);                                         // Determinación de cantidad de mediciones 
        lcd.print("Ingrese cantidad de muestras:");                 // El máximo se debe a que está limtiado por el ancho máximo del array
        lcd.setCursor(10,2);
        lcd.print(measureValues[chosedValue]);

        while (!chosed){
           if (digitalRead(ARRIBA) == LOW && chosedValue < 9){
              chosedValue++;
              lcd.clear();
              lcd.setCursor(0,0);
              lcd.print("Ingrese cantidad de muestras:");
              lcd.setCursor(10,2);
              lcd.print(measureValues[chosedValue]);
              delay(250);
           }
           else if (digitalRead(ABAJO) == LOW && chosedValue > 0){
              chosedValue--;
              lcd.clear();
              lcd.setCursor(0,0);
              lcd.print("Ingrese cantidad de muestras:");
              lcd.setCursor(10,2);
              lcd.print(measureValues[chosedValue]);
              delay(250);
           }
           else if (digitalRead(ENTER) == LOW){
             chosed = true;
             lcd.clear();
             measureQuantity = measureValues[chosedValue];
             if(measureQuantity > maxMeasureQuantity) {                       // Verifico que la cantidad de mediciones a tomar no exceda el ancho del array
               lcd.setCursor(0,0);
               lcd.print("Se excedio la cantidad de mediciones máximas");
               lcd.setCursor(0,2);
               lcd.print("Intente de nuevo");
               goto MEDITION;
             }
           }
        }
  
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Comienza la medicion");
        delay(2000);
  
        //************ TIMER ************//
  
        initTimer1(); //Inicializo el Timer1
  
        //************ COMIENZO TX ************//
  
        for (int i = 0 ; i < measureQuantity ; i++) {       // Iteración para cada medición
           initInterrupt();                                  // NO BORRAR
           startTime = (overflowCount << 16) + TCNT1;        // Almaceno la cantidad de ticks del contador, esta manera de hacerlo es para obtener un valor preciso, teniendo en cuenta la cantidad de overflows y los ticks actuales del contador
           digitalWrite(CS, HIGH);                           // Activo el pulso modulante
  
          //************ ESPERANDO INTERRUPCIÓN ************//
    
          while (!received){
            finishTime = (overflowCount << 16) + TCNT1; // Almacena el tiempo actual del contador del Timer1
      
            if (finishTime - startTime >= timeOut){     // Salta el timeOut
              timeOutReached();
            }
            
            if ((finishTime - startTime >= transmitionTime) && !transmited){  // Dejo de transmitir y solo espero que la señal llegue o salte el timeOut
              digitalWrite(CS, LOW);    // Dejo de transmitir
              transmited = true;        // Dejo en claro que deje de transmitir        
            }  
          }  

          //************ ALMACENAMIENTO ************//
    
          measureTime[i] = (finishTime - startTime) * 62.5 / 1000; // El tiempo medido fue en ticks del contador del Timer1, eso se lo multiplica por 62.5 porque cada tick equivalen a 62.5ns, y se lo divide por 1000 para pasarlo a useg
          received = false; 
          transmited = false;   
    
          //************ VISUALIZACIÓN ************// 
    
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Medicion actual: ");
          lcd.setCursor(5,2);
          lcd.print(measureTime[i]); 
          lcd.print(" us");
    
          if(measureTime[i] > 1000) { // Por si surge un error doy tiempo a que el sistema se acomode
            digitalWrite(CS, LOW);
            delay(5000);
          }
      
          digitalWrite(CS, LOW);  // Dejo de transmitir
          delay(2000);            // Cuando volvemos a medir, si justo quedó un rebote en el canal el circuito lo agarra antes de dejar de transmitir y ahí entra en un loop
        }

        //************ CALCULOS ************//
  
        for (int i=0 ; i < measureQuantity ; i++)
           measureAverage += measureTime[i];
        measureAverage = measureAverage/measureQuantity;
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Valor promedio:");
        lcd.setCursor(5,2);
        lcd.print(measureAverage);
        lcd.print(" us");
        delay(5000);

        //************ ESCRITURA SD ************//
  
        archivo = SD.open("prueba1.txt", FILE_WRITE);             // A partir del objeto file creado, se crea un archivo en la memoria SD
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Escribiendo en SD...");
        if (archivo) {                                            // De abrirse correctamente, comienza a escribirse un archivo en la tarjeta SD
          archivo.print("Mediciones: ");
          archivo.print("[");
          archivo.print(measureQuantity);
          archivo.println("]");
          for (int i=0 ; i < measureQuantity ; i++)
            archivo.println(measureTime[i]); 
          archivo.println();
          archivo.close();                                        // Al terminar la escritura se cierra el archivo
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Escritura finalizada");
        }
        else {
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Error al abrir archivo");
        }

        delay(2500);
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("********FIN*********");

        //************ FIN ************//
  
        while(true){}
  
      break;
 }
}


//************ FUNCIONES ************//

void initTimer1(){                                       // Función encargada de reiniciar e inicializar el Timer1
  TCCR1A = 0;           // Reinicio Timer1, dejando sus registros de control A y B en 0
  TCCR1B = 0;
  TIMSK1 = bit (TOIE1); // Configuro el Timer1 para que cree una interrupción al overflow el contador
  TCNT1 = 0;            // Reinicio el contador del Timer1  
  overflowCount = 0;  
  TCCR1B =  bit (CS10); // Inicio el conteo de trailer a la frecuencia del clock de 16MHz sin preescalares  
}

void initInterrupt(){                                    // Función encargada de inicializar la interrupción que indica que la señal llegó a Rx  
  EIFR = bit (INTF0);                                    // Limpia el flag que indica que hubo una interrupción
  attachInterrupt(digitalPinToInterrupt(RX), eco, LOW);  // Inicializa la interrupción de la señal en Rx     
}

void timeOutReached(){                        // Funcion encargada de ejecutarse cuando se alcanza el time out
  detachInterrupt(digitalPinToInterrupt(RX)); // Desactivo la interrupción
  received = true;                            // Modifico para salir del loop
  finishTime = startTime;                        
  Serial.println("Time out alcanzado");
}

//************ ISR ************//

void eco(){
  finishTime = (overflowCount << 16) + TCNT1; // Almaceno el tiempo actual del contador del Timer1
  received = true;
  detachInterrupt(digitalPinToInterrupt(RX)); // Desactivo la interrupción
}

ISR (TIMER1_OVF_vect){  // ISR de arduino que ingresa cada vez que el contador del Timer1 TCNT1 sufre un overflow y se reinicia
  overflowCount++;
} 
