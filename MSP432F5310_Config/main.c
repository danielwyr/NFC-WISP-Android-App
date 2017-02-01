#include <msp430f5310.h>

unsigned char TXByteCtr;
unsigned char RXByteCtr;
unsigned char RXData;
unsigned char TXData;
unsigned char TXAddr;
unsigned char MODE;
unsigned char *PRxData;                     // Pointer to RX data

void sys_init(void) {
    WDTCTL = WDTPW | WDTHOLD; // Stop watchdog timer

    P2DIR |= 0x02;
    P2SEL |= 0x02;

    P5SEL |= 0x0C;
    UCSCTL6 &= ~XT2OFF;
    UCSCTL3 |= SELREF_2;                      // FLLref = REFO
											 // Since LFXT1 is not used,
											 // sourcing FLL with LFXT1 can cause
											 // XT1OFFG flag to set
    UCSCTL4 |= SELA_2;                        // ACLK=REFO,SMCLK=DCO,MCLK=DCO

    // Loop until XT1,XT2 & DCO stabilizes - in this case loop until XT2 settles
	do
	{
	UCSCTL7 &= ~(XT2OFFG + XT1LFOFFG + DCOFFG);
											// Clear XT2,XT1,DCO fault flags
	SFRIFG1 &= ~OFIFG;                      // Clear fault flags
	}while (SFRIFG1&OFIFG);                   // Test oscillator fault flag

	UCSCTL6 &= ~XT2DRIVE0;                    // Decrease XT2 Drive according to
	                                          // expected frequency
	UCSCTL4 |= SELS_5 + SELM_5;               // SMCLK=MCLK=XT2
}

void I2C_config(void) {
    P4SEL |= 0x06; // Assign P4.1 P4.2 to secondary digital function (ISC_CLK, ISC_SDA)

    UCB1CTL1 |= UCSWRST;    // Enable SW reset
    UCB1CTL0 = 0x00;
    UCB1CTL0 = UCMST + UCMODE_3 + UCSYNC;

    UCB1CTL1 = UCSSEL_2 + UCSWRST; // UCSSEL_2 Clock source 2 (SMCLK)

    UCB1BR0 = 32; // fSCL = SMCLK/12 = ~100kHz (Need to verify [SMCLK = 13.56MHz])
    UCB1BR1 = 0;
    UCB1CTL1 &= ~UCSWRST;   // Clear SW reset, resume operation


}

void I2C_Tx_config(unsigned char slave_addr) {
	UCB1CTL1 |= UCSWRST;
	//UCB1CTL1 |= UCTR; // I2C transmitter mode
	UCB1I2CSA = slave_addr;
	UCB1CTL1 &= ~UCSWRST;   // Clear SW reset, resume operation
	UCB1IE |= UCTXIE + UCNACKIE + UCRXIE;   // Enable TX interrupt
}

#pragma vector = USCI_B1_VECTOR
__interrupt void USCI_B1_ISR(void) {
    switch(__even_in_range(UCB1IV, 12)) {
      case  0: break;                           // Vector  0: No interrupts
      case  2: break;                           // Vector  2: ALIFG
      case  4: break;                           // Vector  4: NACKIFG
      case  6: break;                           // Vector  6: STTIFG
      case  8: break;                           // Vector  8: STPIFG
      case 10:                                  // Vector 10: RXIFG
		RXByteCtr--;                            // Decrement RX byte counter
		if (RXByteCtr >1) {
		*PRxData++ = UCB1RXBUF;               // Move RX data to address PRxData
		} else if (RXByteCtr == 0) {                 // Only one byte left?
		  UCB1CTL1 |= UCTXSTP;                // Generate I2C stop condition
		} else {
		*PRxData = UCB1RXBUF;                 // Move final RX data to PRxData
		__bic_SR_register_on_exit(LPM0_bits); // Exit active CPU
		}
        break;
      case 12:
		if (TXByteCtr > 1) {                          // Check TX byte counter
			UCB1TXBUF = TXAddr;                   // Load TX buffer
			TXByteCtr--;                          // Decrement TX byte counter
		} else if (TXByteCtr > 0) {
			UCB1TXBUF = TXData;
			TXByteCtr--;
		} else {
			UCB1CTL1 |= UCTXSTP;                  // I2C stop condition
			UCB1IFG &= ~UCTXIFG;                  // Clear USCI_B0 TX int flag
			if(MODE) UCB1CTL1 &= ~UCTR;
			__bic_SR_register_on_exit(LPM0_bits); // Exit LPM0
		}
    	    	break;                           // Vector 12: TXIFG
      default: break;
    }
}

void I2C_Tx(unsigned char tx_addr, unsigned char tx_data) {
	MODE = 0x00;
	TXAddr = tx_addr;
	TXData = tx_data;
	TXByteCtr = 0x02;
	while (UCB1CTL1 & UCTXSTP);             // Ensure stop condition got sent
	UCB1CTL1 |= UCTR + UCTXSTT;             // I2C TX, start condition

	__bis_SR_register(LPM0_bits + GIE);     // Enter LPM0 w/ interrupts
	__no_operation();
	__delay_cycles(1000);
}

void I2C_Rx(unsigned char rx_addr) {
	RXByteCtr = 0x03;
	MODE = 0x01;
	TXData = rx_addr;
	TXByteCtr = 0x01;
	while (UCB1CTL1 & UCTXSTP);             // Ensure stop condition got sent
	UCB1CTL1 |= UCTR + UCTXSTT;             // I2C TX, start condition

	__bis_SR_register(LPM0_bits + GIE);     // Enter LPM0 w/ interrupts
	__no_operation();

	while (UCB1CTL1 & UCTXSTP);             // Ensure stop condition got sent
	UCB1CTL1 |= UCTXSTT;                    // I2C start condition


	__bis_SR_register(LPM0_bits + GIE);     // Enter LPM0, enable interrupts
	__no_operation();
	__delay_cycles(1000);
}

void main(void) {
    sys_init();

    I2C_config();
    I2C_Tx_config(0x57);

    I2C_Tx(0x06, 0x03);
    I2C_Tx(0x07, 0x47);
    I2C_Tx(0x09, 0x33);

	while(1) {
		I2C_Rx(0x05);

	}
}
