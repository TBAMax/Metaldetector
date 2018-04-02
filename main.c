/*
 * Project:Metal detector with PIC microcontroller
 * File:   main.c
 * Author: Teemo Berkis
 *
 * Created on pühapäev, 25. märts 2018. a, 4:01
 * for PIC16F1824
 */


#include <xc.h>                           //PIC hardware mapping
#include <stdint.h>         /* For uint8_t definition */
#include <stdbool.h>        /* For true/false definition */
#include "eusart.h"
#define _XTAL_FREQ 32000000              //Used by the XC8 __delay_ms() macro

//timing constants
#define FetPulseTime    140     //in microseconds default  140 us ;Range 25-255
#define FetPauseTime    1840    //in microseconds default 1840 us  (max2000;min600)
#define MeasuringInput  RA3
#define LED             LATAbits.LATA1
#define MOSFET          LATAbits.LATA5
#define Sensitivity     500
#define CompRef         27      //n for start comparator reference voltage:Vref=n*1,024/32(range 0-31))
//actual value is adapted by circuit when it is powered up.
//higher is more sensitive
//config bits that are part-specific for the PIC16F1829
#pragma config FOSC=INTOSC, WDTE=OFF, PWRTE=OFF, MCLRE=OFF, CP=OFF, CPD=OFF, BOREN=NSLEEP, CLKOUTEN=OFF, IESO=OFF, FCMEN=OFF
#pragma config WRT=OFF, PLLEN=ON, STVREN=ON,BORV=LO, LVP=OFF

const uint8_t FetPulse = (255 - FetPulseTime - 4); //convert timing value to corresponding register values 
const uint8_t FetPause = (255 - (FetPauseTime / 16));
volatile bit dataready, datareadyF; //pipeline from interrupts to main prog
bit SoundOn; //Sound on/off switch
volatile unsigned int T_Result;
volatile unsigned int F_Result;
volatile uint8_t PeriodResult;
volatile uint8_t Periodcounter;
unsigned uint8_t SoundTone;
unsigned long milliscount = 0;
unsigned int T_Reference;
unsigned int F_Reference;

//function prototypes;
void AutoAdjustVref(void);
unsigned long millis(void);
unsigned int FastReferenceTime(unsigned int T_result);
unsigned int FastReferenceFreq(unsigned int F_result);
bit calc(void);
bit millis250(void); //interval 250

void main(void) {
    uint8_t MainMode = 0;
    static unsigned int T_Result_copy;
    __delay_ms(2);
    // general initialization       
    OSCCON = 0b11110000; //Configure internal oscillator to 32Mhz
    PORTA = 0; //init port A
    LATA = 0; //init port output latch
    ANSELA = 0; //All Digital I/O    
    MOSFET = 0; //MOSFET LO
    TRISA = 0b00011000; //PORT A directions 0=output  1=input
    //xxxx1xxx    -button
    //xxx1xxxx    -spare coil input
    //xxxxx0x0    -speaker
    //xxxxxx0x    -LED

    PORTC = 0; //init port C
    LATC = 0; //init port output latch
    ANSELC = 0; //All Digital I/O
    ANSELC = 0b00001110; //PORT C pins 1=analog 0=digital
    //xxxxxx1x   -pot1 (RC1)
    //xxxxx1xx   -pot2 (RC2)
    //xxxx1xxx   -comparator input (RC3)
    TRISC = 0b00011110; //PORT C directions 0=output  1=input
    //xx0xxxxx    -serial TX (RC5)
    //xxxxxxx0    -potentiometers supply
    //xxx1xxxx    -serial RX (RC4)
    // alive led
    LED = 1;
    //DAC setup
    DACCON0 = 0b11000000; //positive ref=Vdd
    DACCON1 = CompRef; //initial reference voltage for comparator
    // enable interrupts
    GIE = 1; //general interrupts enable
    TMR0IE = 1; //timer0 interrupts enable
    TMR0IF = 1; //Software generated timer0 interrupt, to get things going
    // warm up delay
    //    __delay_ms(250); 
    // setup comparator 1
    CM1CON0 = 0b10010101;
    //1-------		comparator enable
    //--0-----		comparator output internal only
    //---1----		output inverted
    //-----1--      high speed
    //------0-		hysteresis disabled
    //-------1		sync with timer1
    CM1CON1 = 0b10010011;
    //1-------		rising edge interrupt enable
    //--01----		+input connects to internal DAC Voltage reference
    //------11		-input connects to C1IN1- (RC3)(different from 8pin version!) 

    //setup timer2 (for sound)
    PR2 = 100; //timer 2 period
    T2CON = 0b00000111; //interval 0.8ms
    //-0000---     Postscaler 1:1
    //-----1--     Timer on
    //------11     Prescaler 64
    TMR2 = 0;
    TMR2IF = 0; //clear timer2 interrupt flag
    PEIE = 1; //peripheral interrupt enable
    TMR2IE = 1; //enable timer2 interrupts

    //setup timer4 for millis()
    PR4 = 124; //timer 2 period, interval 1ms
    T4CON = 0b00000111;
    //-0000---     Postscaler 1:1
    //-----1--     Timer on
    //------11     Prescaler 64
    TMR4 = 0;
    TMR4IF = 0; //clear timer4 interrupt flag
    //        PEIE=1;        //peripheral interrupt enable, already enabled
    TMR4IE = 1; //enable timer4 interrupts
    SoundOn = 1; //start beep immediately

    //Automatic comparator level adjustment
    AutoAdjustVref();
    EUSART_Initialize();
    __delay_ms(500);
    while (1) { //main loop
        if (dataready) { //got new data?
            T_Reference = FastReferenceTime(T_Result);
            SoundOn = calc(); //compare
            T_Result_copy=T_Result;
            dataready = 0; //release blocking
        }
        if (datareadyF) { //got new data?
            if(!SoundOn)F_Reference = FastReferenceFreq(F_Result); //only do when there is no sound                    
            if (millis250()) //duties with interval 250ms
            {
//                printf("%d,",DACCON1);
                printf("%d,", F_Reference-F_Result);
                printf("%d\r", T_Reference-T_Result_copy);
//                printf("R%d\r", F_Reference);
            }
            datareadyF = 0; //release blocking            
        }

    }
    return;
}

bit millis250(void) {
    static unsigned long lastmillis;
    if ((millis() - lastmillis) >= 250) {
        lastmillis = millis();
        return 1;
    } else return 0;
}

bit calc(void) {
    if ((T_Result + Sensitivity) < T_Reference) {
        return 1;
    } else return 0;
}

void AutoAdjustVref(void) {
    unsigned long startmillis;
    startmillis = millis();
    SoundTone = 100;
    SoundOn = 1;
    while (!MOSFET); //wait for mosfet high
    while ((millis() - startmillis) >= 500) //time constraint, measurement time period[ms]
    {
        if (!MOSFET) { //wait for mosfet low
            __delay_us(FetPauseTime - 200); //the time of expected oscillation decay
            while (!MOSFET) {
                if (C1OUT) { //if there is still activity detected at the comparator output
                    DACCON1--; //raise the bar(comparator threshold)
                    break; //only once
                }
            }
            while (!MOSFET); //wait rest of the cycle 
        }
    }
    SoundOn = 0;
}

unsigned long millis(void) {
    unsigned long currentmillis;
    TMR4IE = 0; //disable timer4 interrupts
    currentmillis = milliscount; //take snapshot
    TMR4IE = 1; //enable timer4 interrupts, this is where the real counting happens
    return currentmillis;
}

//sums up 64 values (64calls to this function)
//divides by 64 to get the average
//writes to the circular buffer
//finds maximum element in buffer

unsigned int FastReferenceTime(unsigned int T_result) {
    static unsigned int queue[32] = {0}; //32 element buffer
    static unsigned long avg = 0;
    static unsigned char i; //loop
    static unsigned char counter1 = 0;
    static unsigned int buffermax; //max element in buffer
    unsigned char u;

    avg += T_result;
    counter1++;
    if (counter1 == 64) //it is time to update ring buffer
    {
        counter1 = 0;
        avg = avg / 64; //divide by 64
        if (i >= 32) i = 0; //roll back ring            
        queue[i] = (unsigned int) avg; //insert new value to the ring
        i++;
        buffermax = queue[0];
        for (u = 1; u < 32; u++) {
            if (queue[u] > buffermax)buffermax = queue[u];
        }
    }
    return buffermax;
}

//sums up 64 input values
//divides by 64 to get the average
//writes to the circular buffer
//finds average of the buffer(recursive algorithm)

unsigned int FastReferenceFreq(unsigned int F_result) {
    static unsigned int queue[32] = {0}; //32 element buffer
    static unsigned long avg = 0;
    static unsigned uint8_t i; //loop
    static unsigned uint8_t counter1 = 0;
    static unsigned long buffersum; //sum of buffer elements

    avg += F_result;
    counter1++;
    if (counter1 >= 64) //it is time to update ring buffer
    {
        counter1 = 0;
        avg = avg / 64; //divide by 64
        if (i >= 32) i = 0; //roll back ring
        buffersum -= queue[i]; //subtract the current value out of the sum
        buffersum += avg; //insert new value to the sum
        queue[i] = (unsigned int) avg; //also insert new value to the ring
        i++;
        avg = 0;
    }
    return (buffersum / 32);
}
