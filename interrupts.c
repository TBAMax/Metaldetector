/******************************************************************************/
/*Files to Include                                                            */
/******************************************************************************/
#include <xc.h>
//#include <pic16f1824.h>                                     //PIC hardware mapping
#include <stdint.h>         /* For uint8_t definition */
#include <stdbool.h>        /* For true/false definition */
#include "eusart.h"

#define MOSFET      LATAbits.LATA5                  //output devices
#define PiezoPin1      LATAbits.LATA0
#define PiezoPin2      LATAbits.LATA2
/******************************************************************************/
/* global variables from other files that are used here                       */
/******************************************************************************/
extern uint8_t FetPulse; //MOSFET ON time 
extern uint8_t FetPause; //MOSFET OFF time
extern bit dataready, datareadyF; //pipeline flags from interrupts to main prog
extern bit SoundOn; //Sound on/off switch
extern unsigned uint8_t SoundTone; //sound tone
extern unsigned int T_Result;
extern unsigned int F_Result;
extern unsigned uint8_t PeriodResult;
extern unsigned uint8_t Periodcounter;
extern unsigned uint8_t SoundTone;
extern unsigned long milliscount;
/******************************************************************************/
/* Interrupt Routines                                                         */

/******************************************************************************/


void interrupt isr(void) {
    static uint8_t Soundflow;

    /* Determine which flag generated the interrupt */
    if (INTCONbits.T0IF) //timer0 interrupt, MOSFET timing
    {
        if (!MOSFET) { //starts MOSFET pulse
            OPTION_REG = 0b11000010; //configure timer0
            //--0-----          timer mode (T0CS = 0), internal instruction clock(8 MHz)
            //----0---          prescaler assigned to Timer0 (PSA = 0)
            //-----010          prescale = 8
            TMR0 = FetPulse; //load timer0 with FetPulseTime, starting timer
            INTCONbits.T0IF = 0; /* Clear Interrupt Flag */

            //save timings collected from previous cycle
            T1CONbits.TMR1ON = 0; //timer1 stop
            T1GCONbits.TMR1GE = 0;
            if (!dataready) {
                T_Result = TMR1; //read timer1
                PeriodResult = Periodcounter;
                dataready = 1;
            }
            TMR1 = 0;
            MOSFET = 1;
            PIE2bits.C1IE = 0; //comparator interrupt disable during MOSFET ON
            C1INTP = 0; //Comparator rising edge interrupt disabled
            PIR2bits.C1IF = 0;
        } else { //stops MOSFET pulse
            while (!(TMR0 & (1 << 2))); //wait extra4us for accurate timing(test bit 2 for that)
            PIE2bits.C1IE = 1; //comparator interrupt enable
            PIR2bits.C1IF = 0;
            C1INTP = 1; //Comparator rising edge interrupt enable
            //turning off MOSFET will instantly cause rising edge... no problems
            MOSFET = 0; //turn off MOSFET
            T1CON = 0b01000001; //start timer1            
            OPTION_REG = 0b11000110; //configure timer0
            //--0-----          timer mode (T0CS = 0), sisemine instruktsiooni kell(8 MHz)
            //----0---          prescaler assigned to Timer0 (PSA = 0)
            //-----110          prescale = 128
            TMR0 = FetPause;
            INTCONbits.T0IF = 0; /* Clear Interrupt Flag */
            Periodcounter = 0; //clear periodcounter
        }
    } else if (PIR2bits.C1IF) //comparator interrupt, Periods counting
    {
        PIR2bits.C1IF = 0; /* Clear Comparator Interrupt Flag*/
        Periodcounter++;
        if (Periodcounter == 19) {//activate timer1 gate function so that timer stops at next rising edge (without stopping timer now)
            T1GCONbits.T1GPOL = 1;
            T1GCON = 0b01111010;
            //0-------		;gate function disabled TMR1GE
            //-1------		;gate active high
            //--11----		;gate single pulse and toggle mode
            //------10		;gate input from comparator
            //'----1---'		;t1ggo
            T1GCONbits.T1GPOL = 0;
            T1GCONbits.T1GPOL = 1; //force start gate circuit by toggling polarity
            T1GCONbits.TMR1GE = 1; //switch to gate control
        } else if (Periodcounter == 20) {
            if (datareadyF == 0) {
                F_Result = TMR1; //read timer1
                datareadyF = 1;
            }
            TMR1 = 0; //clear timer1
            T1GCON = 0b10000010;
            //1-------		;gate function enabled TMR1GE
            //-0------		;gate active low
            //------10		;gate input from comparator
            T1CON = 0b01000001;
            //01------		timer1 clock source is system clock 32MHz
            //--00----'		prescale off(1:1)
            //----xxx-'		
            //-------1'		timer1 enable TMR1ON
            PIE2bits.C1IE = 0; //Disable further comparator interrupts for this cycle.
            C1INTP = 0; //Comparator rising edge interrupt disabled
        }
    } else if (PIR1bits.TMR2IF) //timer2 interrupt, SOUND
    {
        PIR1bits.TMR2IF = 0; //clear timer2 interrupt flag
        if (SoundOn) {
            switch (Soundflow) { //dual tone sound generator
                case 0:
                    PR2 = 80; //basic tone of about 630Hz
                    PiezoPin1 = 1;
                    PiezoPin2 = 0;
                    Soundflow++;
                    break;
                case 1:
                    PR2 = 80;
                    PiezoPin1 = 0;
                    PiezoPin2 = 1;
                    Soundflow++;
                    break;
                case 2:
                    PR2 = SoundTone; //variable tone component
                    PiezoPin1 = 1;
                    PiezoPin2 = 0;
                    Soundflow++;
                    break;
                case 3:
                    PR2 = SoundTone;
                    PiezoPin1 = 0;
                    PiezoPin2 = 1;
                    Soundflow = 0;
                    break;
                default:
                    Soundflow = 0;
                    break;
            }
        } else { //silent
            PR2 = 80;
            PiezoPin1 = 0;
            PiezoPin2 = 0;
        }
    } else if (TMR4IF) //timer4 interrupt. For millis()
    {
        milliscount++;
        TMR4IF = 0;
    } else if (PIE1bits.TXIE == 1 && PIR1bits.TXIF == 1){ // UART interrupt              
        EUSART_Transmit_ISR();
    } else if (PIE1bits.RCIE == 1 && PIR1bits.RCIF == 1) {
        EUSART_Receive_ISR();
    } else {
        NOP(); /* Unhandled interrupts */
        RESET();
    }
}