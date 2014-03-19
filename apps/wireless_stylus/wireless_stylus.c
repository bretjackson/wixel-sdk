/** Wireless_stylus app:

Wireless stylus app with two buttons.
Uses PM3 low power mode to sleep until it recieves an interrupt from a button press.

NOTE: this program is currently set to only report one button press at a time. It will not work if both buttons are pressed together.

== Pinout ==

P1_2 = button 1 input (pulled high; low means button is pressed), pin 12
P1_3 = button 2 input (pulled high; low means button is pressed), pin 13

== Parameters ==

param_radio_channel = channel number for radio transmissions. The receiver must be on the same channel.
						Valid values are from 0 to 255. To avoid interference, the channel numbers of 
						different Wixel pairs operating in the should be at least 2 apart. (This is a
						Wixel App parameter; the user can set it using the Wixel Configuration Utility.)

*/

#include <wixel.h>
#include <stdio.h>
#include <usb.h>
#include <usb_com.h>
#include <usb_hid_constants.h>
#include <radio_registers.h>

#define BUTTON_1 12
#define BUTTON_2 13

#define TX_INTERVAL 10 // time between transmissions (ms)

int32 CODE param_radio_channel = 128;

// This definition should match wireless_stylus_receiver.h
#define RADIO_PACKET_SIZE 3

static volatile XDATA uint8 packet[1 + RADIO_PACKET_SIZE];
static volatile BIT radioDone = 0;

static uint8 DATA currentBurstId = 0;
static uint16 lastBurst = 0;

/* FUNCTIONS ******************************************************************/

void txInit()
{
    uint8 i;

    radioRegistersInit();

    CHANNR = param_radio_channel;

    PKTLEN = RADIO_PACKET_SIZE;
    
    MCSM0 = 0x14;    // Auto-calibrate when going from idle to RX or TX.
    MCSM1 = 0x00;    // Disable CCA.  After RX, go to IDLE.  After TX, go to IDLE.
    // We leave MCSM2 at its default value.

	IEN2 |= 0x01;    // Enable RF general interrupt
    RFIM = 0xF0;     // Enable these interrupts: DONE, RXOVF, TXUNF, TIMEOUT

    EA = 1;          // Enable interrupts in general

    IOCFG2 = 0b011011; // put out a PA_PD signal on P1_7 (active low when the radio is in TX mode)

    dmaConfig.radio.DC6 = 19; // WORDSIZE = 0, TMODE = 0, TRIG = 19

    dmaConfig.radio.SRCADDRH = (unsigned int)packet >> 8;
    dmaConfig.radio.SRCADDRL = (unsigned int)packet;
    dmaConfig.radio.DESTADDRH = XDATA_SFR_ADDRESS(RFD) >> 8;
    dmaConfig.radio.DESTADDRL = XDATA_SFR_ADDRESS(RFD);
    dmaConfig.radio.LENL = 1 + RADIO_PACKET_SIZE;
    dmaConfig.radio.VLEN_LENH = 0b00100000; // Transfer length is FirstByte+1
    dmaConfig.radio.DC7 = 0x40; // SRCINC = 1, DESTINC = 0, IRQMASK = 0, M8 = 0, PRIORITY = 0
    
    for(i = 1; i < sizeof(packet); i++)
    {
        packet[i] = 'A' + i;
    }
    packet[0] = RADIO_PACKET_SIZE;

    RFST = 4;  // Switch radio to Idle mode. (SIDLE mode)
}


void sendButtonStatus()
{
    if (MARCSTATE == 1)
    {
		packet[1] = currentBurstId;
        packet[2] = isPinHigh(BUTTON_1)? '0' :'1';//(!isPinHigh(BUTTON_1) << MOUSE_BUTTON_LEFT) | (!isPinHigh(BUTTON_2) << MOUSE_BUTTON_RIGHT); 
        packet[3] = isPinHigh(BUTTON_2)? '0' :'1';

		currentBurstId++;

        RFIF &= ~(1<<4);                   // Clear IRQ_DONE
        DMAARM |= (1<<DMA_CHANNEL_RADIO);  // Arm DMA channel
        RFST = 3;                          // Switch radio to TX (STX mode)
    }
}

// This is the radio interrupt. It is called when there is an underflow or the radio has completed sending the tx packet
ISR(RF, 0)
{
    S1CON = 0; // Clear the general RFIF interrupt registers

    if (RFIF & 0x10) // Check IRQ_DONE
    {
        radioDone = 1;
		//clear the IRQ_DONE flag
		RFIF &= ~0x10;
    }
}

void setupButtonInterrupt()
{
	P1DIR &= ~0x0C;     // DIRP1_[2-3] = 0 (P1_2 and P1_3 is an input)
	PICTL |= 0x02;      // P1ICON = 1 (Falling edge interrupt on Port 1)

	// clear port 1 interrupt status flag
    P1IFG = 0;     // P1IFG.P1IF[7:0] = 0
    // clear cpu interrupt flag
    IRCON2 &= ~0x08;    // IRCON2.P1IF = 0    
    // set port 1 interrupt mask
    P1IEN = 0x0C;      // P1IEN.P1_[2-3]IEN = 1
    // select rising edge interrupt on port 1
    //PICTL &= ~0x02;     // PICTL.P1ICON = 0
    // enable port 1 interrupt
    IEN2 |= 0x10;       // IEN2.P1IE = 1
}

ISR (P1INT, 0)
{
    // clear port 1 interrupt status flag
    P1IFG = 0;          // P1IFG.P1IF[7:0] = 0
    // clear cpu interrupt flag
    IRCON2 &= ~0x08;    // IRCON2.P1IF = 0
   // clear sleep mode 
    SLEEP &= ~0x03;     // SLEEP.MODE = 11
    // clear port 1 interrupt mask
    P1IEN = 0;          // P1IEN.P1_[7:0]IEN = 0
    
	//Note, I don't think we want to do this since the radio depends on the interrupt
	// disable port 1 interrupt
    //IEN2 &= ~0x10;      // IEN2.P1IE = 0
}

void putToSleep()
{
	// Turn off the radio
	/** Disarm the DMA channel. ************************************************/
    DMAARM = 0x80 | (1<<DMA_CHANNEL_RADIO); // Abort any ongoing radio DMA transfer.
    DMAIRQ &= ~(1<<DMA_CHANNEL_RADIO);      // Clear any pending radio DMA interrupt

	 /** Clear the some flags from the radio ***********************************/
    // We want to do it before restarting the radio (to avoid accidentally missing
    // an event) but we want to do it as long as possible AFTER turning off the
    // radio.
    RFIF = ~0x30;  // Clear IRQ_DONE and IRQ_TIMEOUT if they are set.

	// select sleep mode PM3
    SLEEP |= 0x03;  // SLEEP.MODE = 11
    // 3 NOPs as specified in 12.1.3
    __asm nop __endasm;
    __asm nop __endasm;
    __asm nop __endasm;
    // enter sleep mode
    if (SLEEP & 0x03) PCON |= 0x01; // PCON.IDLE = 1

	// Turn the radio back on
	DMAARM |= (1<<DMA_CHANNEL_RADIO);  // Arm DMA channel
    RFST = 4;                          // Switch radio to IDLE
}

void main()
{
	systemInit();
    
    // set P1_0 and P1_1 to outputs to avoid leakage current
    setDigitalOutput(10, LOW);
    setDigitalOutput(11, LOW);
    
    if (usbPowerPresent())
    {
        usbInit();
        
        while (1)
        {
            usbShowStatusWithGreenLed();
            boardService();
            usbComService();
        }
    }
	else
	{
		// Show the yellow led so we know we are in this mode
		LED_YELLOW(1);
        delayMs(300);
        LED_YELLOW(0);

		txInit();

        while (1)
        {
			boardService();

			do
            {
                setupButtonInterrupt();
                putToSleep();
                        
                // on wake, wait and check again to debounce
                delayMs(25);
            } while (isPinHigh(BUTTON_1) && isPinHigh(BUTTON_2));
			
			radioDone = 0;
			sendButtonStatus(); // Send button down
			
			// Wait until the button is released and the radio message has been sent
			while(!isPinHigh(BUTTON_1) || !isPinHigh(BUTTON_2) || !radioDone){}
			//wait and check again to debounce
			delayMs(25);
			while(!isPinHigh(BUTTON_1) || !isPinHigh(BUTTON_2)){}
			
			radioDone = 0;
			sendButtonStatus(); // send button up
			
			// Wait until the radio is done sending before going back to sleep
			// Radio done is set to 1 in the RF interrupt
			while(!radioDone) {}
		}
	}
}
