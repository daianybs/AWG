//GERADOR DE ONDAS ARBITR�RIAS CONTROLADO POR APLICATIVO M�VEL
//C�DIGO DESENVOLIDO NO CCS PARA MICROCONTROLADOR MSP430F5529

//AUTORES: Daiany Besen e Giovani Blanco Bartnik
//ANO: 2021

//DESCRI��O:
//Este c�digo implementa a t�cnica de S�ntese Digital Direta (DDS) para a gera��o de sinais.
//Utilizando o Banco D da mem�ria flash do microcontrolador � implementada uma LUT de
//14 bits, armazenando 16384 amostras do sinal que ser� gerado. Para o acumulador de fase (AF)
//� utilizada uma vari�vel de 32 bits, onde apenas 19 bits s�o utilizados sendo que o 5 bits
//menos significativos s�o truncados. A ftw � descrita como uma vari�vel de 16 bits.

//As amostas do sinal s�o recebidas atrav�s do protocolo SPI por uma rotina de interrup��o;
//da mesma forma que o valor de FTW. Tais amostras s�o ent�o alocadas na LUT implementada.
//Um ciclo de gera��o foi desenvolvido, permitindo que as amostas sejam enviadas para o
//DAC conectado em saidas do PORT A do microcontrolador.
//
//
//MSP430F5529 SPI
// P3.0|-> Data Out (UCB0SIMO)
// P3.1|<- Data In (UCB0SOMI)
// P3.2|-> Serial Clock Out (UCB0CLK)

//MSP430F5529 DAC
// P1.0
// P1.1
// P1.2
// P1.3
// P1.4
// P1.5
// P1.6
// P1.7
// P2.0
// P2.1
// P2.2
// P2.3

//******************************************************************************

#include <msp430.h>
#include <stdint.h>
#include <intrinsics.h>
#include <math.h>
#include <stdio.h>


unsigned short int msb = 0;             //byte mais significativo da amostra
unsigned short int lsb = 0;             //byte menos significativo da amostra
unsigned short int qtd_amostras = 16384;
unsigned short int indice_amostras = 0;
unsigned short int *Flash_ptrD;         //ponteiro para o primeiro endere�o a ser escrito na flash
unsigned short int FTW = 1;             //palavra de ajuste de frequancia
unsigned short int FTW_AUX = 0;         //palavra de ajuste de frequencia
unsigned short int F = 0;

unsigned short int msb_ftw = 0;         //parte mais significativa de FTW
unsigned short int lsb_ftw = 0;         //parte menos significativa de FTW
int so_ftw = 0;                         //flag indicando s� o recebimento de FTW
int buf = 0;                            //recebe o primeiro valor da comunica��o spi
unsigned short int value_to_read = 0;   //recebe o valor da leitura da flash
int leitura = 0;                        //flag indicando que a flash ja est� toda escrita e pronta para ser lida
int inicio = 0;                         //flag de inicio do recebimento das informa��es atrav�s do SPI

//vari�veis auxiliares
int i = 0;
int k = 0;
int j = 0;
int aux = 0;




//fun��o que aumenta Vcore permitindo que o oscilador interno
//do microcontrolador trabalhe em 25MHz
void SetVcoreUp (unsigned int level)
{
  // Open PMM registers for write
  PMMCTL0_H = PMMPW_H;
  // Set SVS/SVM high side new level
  SVSMHCTL = SVSHE + SVSHRVL0 * level + SVMHE + SVSMHRRL0 * level;
  // Set SVM low side to new level
  SVSMLCTL = SVSLE + SVMLE + SVSMLRRL0 * level;
  // Wait till SVM is settled
  while ((PMMIFG & SVSMLDLYIFG) == 0);
  // Clear already set flags
  PMMIFG &= ~(SVMLVLRIFG + SVMLIFG);
  // Set VCore to new level
  PMMCTL0_L = PMMCOREV0 * level;
  // Wait till new level reached
  if ((PMMIFG & SVMLIFG))
    while ((PMMIFG & SVMLVLRIFG) == 0);
  // Set SVS/SVM low side to new level
  SVSMLCTL = SVSLE + SVSLRVL0 * level + SVMLE + SVSMLRRL0 * level;
  // Lock PMM registers for write access
  PMMCTL0_H = 0x00;
}



int main(void)
{

    /**************** CONFIGURA��ES GERAIS ******************/

    WDTCTL = WDTPW+WDTHOLD;              // Stop watchdog timer
    PADIR = 0xFFF;                       // 12 bits menos singnificativos do PORTA como sa�da

    /************* CONFIGURA��ES DE FREQUENCIA **************/

    //Aumenta Vcore para o n�vel 3 para suportar fsystem=25MHz
    //� necess�rio incrementar um n�vel por vez
    SetVcoreUp (0x01);
    SetVcoreUp (0x02);
    SetVcoreUp (0x03);

    UCSCTL3 = SELREF_2;                  // Set DCO FLL reference = REFO
    UCSCTL4 |= SELA_2;                   // Set ACLK = REFO

    __bis_SR_register(SCG0);             // Disable the FLL control loop
    UCSCTL0 = 0x0000;                    // Set lowest possible DCOx, MODx
    UCSCTL1 = DCORSEL_7;                 // Select DCO range 50MHz operation
    UCSCTL2 = FLLD_0 + 762;              // Set DCO Multiplier for 25MHz
                                         // (N + 1) * FLLRef = Fdco
                                         // (762 + 1) * 32768 = 25MHz
                                         // Set FLL Div = fDCOCLK/2
    __bic_SR_register(SCG0);             // Enable the FLL control loop

    // Worst-case settling time for the DCO when the DCO range bits have been
    // changed is n x 32 x 32 x f_MCLK / f_FLL_reference. See UCS chapter in 5xx
    // UG for optimization.
    // 32 x 32 x 25 MHz / 32,768 Hz ~ 780k MCLK cycles for DCO to settle
    __delay_cycles(782000);

    // Loop until XT1,XT2 & DCO stabilizes - In this case only DCO has to stabilize
    do
    {
        UCSCTL7 &= ~(XT2OFFG + XT1LFOFFG + DCOFFG); // Clear XT2,XT1,DCO fault flags
        SFRIFG1 &= ~OFIFG;                          // Clear fault flags
    }while (SFRIFG1&OFIFG);                        // Test oscillator fault flag

    /**************** CONFIGURA��ES SPI **********************/


    P3SEL |= BIT0+BIT1+BIT2;              // Portas SPI do modulo USCIB0
    UCB0CTL1 |= UCSWRST;                  // Put state machine in reset
    UCB0CTL0 |= UCSYNC+UCCKPL+UCMSB;      // 3-pin, 8-bit SPI slave,
    UCB0CTL1 &= ~UCSWRST;                 // Initialize USCI state machine
    UCB0IE |= UCRXIE;                     // Habilita USCI_B0 RX interrup��o
    __bis_SR_register(GIE);               // Habilita interrup��es


    unsigned short int *ptr_dac;
    //Acumulador de fase (AF) de 19 bits (vari�vel utilizada � de 32 bits)
    unsigned long int acumulador = 0;


    //Ciclo de Gera��o
    while(1)
    {

        /*
        //ciclo dde gera��o para AF = 14 bits -> mesmo tamanho da LUT
        //Lut 14 bits armazena 16384 amostras, por�m, a flash do MSP430 salva em 32767 endere�os

        //0x01c400 = endere�o inicial do Banco D
        ptr_dac = (0x01c400+(acumulador));

        PAOUT = *ptr_dac;

        acumulador = (acumulador + palavra) & 0x7fff;

        //PROBLEMA -> apesar de mostar no debuger que consome 24 ciclos, na realidade consome 40;
        */

        /*PAOUT = 0b111111111111;
        __delay_cycles(50000);
        PAOUT = 0b000000000000;
                __delay_cycles(50000);*/



        //O ponteiro ptr_dac aponta sequencialmente para para as amostras na LUT
        //0x01c400 � o endere�o inicial do Banco D da mem�ria Flash
        //O endere�o inicial � somado com o valor do acumulador de fase
        //(acumulador >> 5) trunca 5 bits do acumulador de 19 bits
        //como s�o utilizados endere�os de 16 bits, sendo que o micro os endere�a de 8 em 8 bits
        //s�o utilizados 32768 (2^15) endere�os, apesar de salvas apenas 16384 (2^14) amostras
        ptr_dac = (0x01c400+(acumulador >> 5));

        //Envia para a sa�da o valor da LUT onde o ponteiro prt_dac aponta
        PAOUT = *ptr_dac;

        //Incremento do acumulador de fase no valor de FTW
        //Faz uma mascara no acumulador ao realizar uma opera��o AND bit a bit com o valor 0xFFFFF
        //Esta mascara � respons�vel por estourar a contagem quando acululador chega em 1048576 (2^20)
        //como a LUT utiliza o dobro de endere�os, o acumulador tamb�m deve contar o dobro de valores (2^20)
        acumulador = (acumulador + (FTW)+F) & 0xFFFFF ;

        //este ciclo de gera��o consome 68 ciclos

    }
}

//Rotina de interrup��o
//Respons�vel por receber e alocar as amostas recebidas atrav�s da comunia��o SPI
//Utiliza o SPI USCIB0
#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=USCI_B0_VECTOR
__interrupt void USCI_B0_ISR(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(USCI_B0_VECTOR))) USCI_B0_ISR (void)
#else
#error Compiler not supported!
#endif
{
  switch(__even_in_range(UCB0IV,4))        //interrup��o RX
  {
    case 0:break;                          // Vector 0 - no interrupt
    case 2:                                // Vector 2 - RXIFG

        while (!(UCB0IFG&UCTXIFG));        // USCI_B0 TX buffer ready?

        //primeira mensagem recebida => informa o inicio do processo de recebimento
        if (inicio==0)
        {
            // indica que as proximas mensagens recebidas ser�o dados
            inicio = 1;

            //le o valor da primeira mensagem para a vari�vel buf
            buf = UCB0RXBUF;

            aux = 1;

            //se for 1 => ser� enviado todas as 16384 amostras + FTW
            if(buf==1)
            {
                Flash_ptrD = (unsigned short int* )0x01c400;  //endere�o inicial do Bank D da mem�ria principal (flash)
                FCTL3 = FWKEY;                                // Clear Lock bit
                FCTL1 = FWKEY+MERAS;                          // Seta MERAS e limpa ERASE (apaga o BANK D (32KB) inteiro)
                *Flash_ptrD = 0;                              // Limpa a flash
                while(BUSY & FCTL3){}                         // Aguarda terminar de apagar
                FCTL1 = FWKEY+WRT;                            // Habilita a escrita
                so_ftw = 0;
            }
            //se n�o for 1 => ser� recebido s� o valor de FTW
            else
            {
                so_ftw = 1;
            }
        }
        // inicio � zero => inicio do recebimento dos dados
        else
        {
            //ser� recebido primeiramente o valor de FTW
            if (aux == 1)
            {
                /*if (k == 0)
                {
                    msb_ftw = UCB0RXBUF;            //recebe a primera parte do valor de FTW
                    k = 1;
                }
                else
                {
                    lsb_ftw = UCB0RXBUF;                //recebe a ultima parte do valor de FTW
                    FTW = ((msb_ftw<<8)|(lsb_ftw))*2;   //junta os dois bytes na mesma vari�vel
                    //O incremento de FTW deve ser multiplicado por dois, para poder endere�ar as 2^14 amostras
                    //salvas em 2^15 endere�os

                    //se o recebimento est� habilitado somente para FTW
                    if (so_ftw==1)
                    {
                        inicio = 0;                 //volta para o inicio

                    }
                    aux = 0;                        //com aux=0 n�o entrar� nesse if ent�o as amostras ser�o recebidas
                    k = 0;
                }*/
                if(k==0)
                {
                    FTW_AUX = UCB0RXBUF;
                    if(FTW_AUX==1)
                    {
                        F = 65535;
                    }
                    else
                    {
                        F = 0;
                    }
                    k = 1;
                }
                else if (k == 1)
                {
                    msb_ftw = UCB0RXBUF;            //recebe a primera parte do valor de FTW
                    k = 2;
                }
                else
                {
                    lsb_ftw = UCB0RXBUF;                //recebe a ultima parte do valor de FTW
                    FTW = ((msb_ftw<<8)|(lsb_ftw));   //junta os dois bytes na mesma vari�vel
                    //O incremento de FTW deve ser multiplicado por dois, para poder endere�ar as 2^14 amostras
                    //salvas em 2^15 endere�os

                    //se o recebimento est� habilitado somente para FTW
                    if (so_ftw==1)
                    {
                        inicio = 0;                 //volta para o inicio

                    }
                    aux = 0;                        //com aux=0 n�o entrar� nesse if ent�o as amostras ser�o recebidas
                    k = 0;
                }

            }
            //Se n�o for somente FTW, receber as 16384 amostras (cada uma vez em 2 bytes)
            else if (i==0)
            {
                msb = UCB0RXBUF;                    //recebe a primera parte do valor da amostra
                i = 1;
            }
            else
            {
                lsb = UCB0RXBUF;                    //recebe a ultima parte do valor da amostra
                *Flash_ptrD++ = (msb<<8)|(lsb);     //escreve a amostra no atual endere�o da flash e aponta para o proximo endere�o
                indice_amostras++;                  //incrementa o indice das amostras, que vai at� 16384
                i = 0;

                if (indice_amostras == qtd_amostras) //terminou de receber as amostras
                {
                    inicio = 0;                      //limpa a flag de inicio
                    indice_amostras = 0;             //reinicia o indice das amostras
                    leitura = 1;                     //seta a flag de leitura
                    FCTL1 = FWKEY;                   //desabilita a escrita na flash
                    FCTL3 = FWKEY+LOCK;              // Set LOCK bit
                }
            }
        }

    break;

    case 4:break;

    default: break;
  }
}
