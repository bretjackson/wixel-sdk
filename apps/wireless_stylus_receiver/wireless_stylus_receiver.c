/** wireless_stylus_receiver app:

Receives signals from the wireless_stylus app and reports them to the
computer using its USB HID interface.

*/

#include <wixel.h>
#include <usb.h>
#include <usb_hid.h>
#include <usb_com.h>
//#include <radio_queue.h>
#include <radio_registers.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int32 CODE param_radio_channel = 128;

// uncomment if you want to use the hid interface rather than a com port
//#define USE_HID 1

// This definition should be the same in both test_radio_signal_tx.c and test_radio_signal_rx.c.
#define RADIO_PACKET_SIZE 3

static volatile XDATA uint8 packet[1 + RADIO_PACKET_SIZE + 2];

static uint8 DATA currentBurstId = 0;
static uint8 DATA messageStartByte = '!'; 

void updateLeds()
{
    usbShowStatusWithGreenLed();

	//LED_YELLOW(radioQueueRxCurrentPacket());

	LED_RED(0);
}

void rxInit()
{
    radioRegistersInit();

    CHANNR = param_radio_channel;

    PKTLEN = RADIO_PACKET_SIZE;

    MCSM0 = 0x14;    // Auto-calibrate when going from idle to RX or TX.
    MCSM1 = 0x00;    // Disable CCA.  After RX, go to IDLE.  After TX, go to IDLE.
    // We leave MCSM2 at its default value.

    dmaConfig.radio.DC6 = 19; // WORDSIZE = 0, TMODE = 0, TRIG = 19

    dmaConfig.radio.SRCADDRH = XDATA_SFR_ADDRESS(RFD) >> 8;
    dmaConfig.radio.SRCADDRL = XDATA_SFR_ADDRESS(RFD);
    dmaConfig.radio.DESTADDRH = (unsigned int)packet >> 8;
    dmaConfig.radio.DESTADDRL = (unsigned int)packet;
    dmaConfig.radio.LENL = 1 + PKTLEN + 2;
    dmaConfig.radio.VLEN_LENH = 0b10000000; // Transfer length is FirstByte+3
    dmaConfig.radio.DC7 = 0x10; // SRCINC = 0, DESTINC = 1, IRQMASK = 0, M8 = 0, PRIORITY = 0

    DMAARM |= (1<<DMA_CHANNEL_RADIO);  // Arm DMA channel
    RFST = 2;                          // Switch radio to RX mode.
}

void rxMouseState(void)
{
	if (RFIF & (1<<4))
    {
        if (radioCrcPassed())
        {
            currentBurstId = packet[1];

#ifndef USE_HID
			if (usbComTxAvailable() >= 64) {

				uint8 XDATA report[64];
				// format: burst id, last byte represents button states
				uint8 reportLength = sprintf(report, "%c%c%c%c", messageStartByte, packet[1], packet[2], packet[3]);
				usbComTxSend(report, reportLength);
			}
			else {
				LED_RED(1);
				delayMs(25);
				LED_RED(0);
			}
#else
			usbHidMouseInput.x = 0;
			usbHidMouseInput.y =  0;
			usbHidMouseInput.buttons = (packet[2] - '0') | ((packet[3] - '0') << 1);
			usbHidMouseInputUpdated = 1;
#endif
        }
        else
        {
            LED_RED(1);
			delayMs(25);
			LED_RED(0);
        }

        RFIF &= ~(1<<4);                   // Clear IRQ_DONE
        DMAARM |= (1<<DMA_CHANNEL_RADIO);  // Arm DMA channel
        RFST = 2;                          // Switch radio to RX mode.
    }
}

void main()
{
    systemInit();
    usbInit();

	rxInit();

    while(1)
    {
        boardService();
		updateLeds();
        
#ifdef USE_HID
		usbHidService();
#else
		usbComService();
#endif

        rxMouseState();
    }
}
