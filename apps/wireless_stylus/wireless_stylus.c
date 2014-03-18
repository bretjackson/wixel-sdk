/** Wireless_stylus app:

Wireless stylus app with two buttons.
Uses PM3 low power mode to sleep until it recieves an interrupt from a button press.
Then it goes back to sleep until the button is released.

Based on the test_radio_sleep app to shut down the radio when sleeping to save more power.

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
#include <sleep.h>
#include <usb_hid_constants.h>
#include <radio_queue.h>

#define BUTTON_1 12
#define BUTTON_2 13

/* FUNCTIONS ******************************************************************/

void sendButtonStatus()
{
	uint8 XDATA * txBuf = radioQueueTxCurrentPacket();
   if (packet != 0)
   {
       txBuf[0] = 1;   // must not exceed RADIO_QUEUE_PAYLOAD_SIZE
	   txBuf[1] = (!isPinHigh(BUTTON_1) << MOUSE_BUTTON_LEFT) | (!isPinHigh(BUTTON_2) << MOUSE_BUTTON_RIGHT);
       radioQueueTxSendPacket();
   }
}

// setting PICTL bit to 0 seems to trigger on BOTH rising and falling edges, while setting to 1 captures neither!
// http://forum.pololu.com/viewtopic.php?f=30&t=6319&p=30238 has a solution, but I think we want to to do both so we can use the
// same interrupt to for both pressing and releasing the buttons to wake up from low power
void setupButtonInterrupt()
{
    // clear port 1 interrupt status flag
    P1IFG = 0;     // P1IFG.P1IF[7:0] = 0
    // clear cpu interrupt flag
    IRCON2 &= ~0x08;    // IRCON2.P1IF = 0    
    // set port 1 interrupt mask
    P1IEN = 0x0C;      // P1IEN.P1_[2-3]IEN = 1
    // select rising edge interrupt on port 1
    PICTL &= ~0x02;     // PICTL.P1ICON = 0
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
    // disable port 1 interrupt
    IEN2 &= ~0x10;      // IEN2.P1IE = 0
}

void putToSleep()
{
    radioMacSleep();
    sleepMode3();
    radioMacResume();
}

void handleButton1Release()
{
	//Button 1 has been pressed. Sleep until it is released or button 2 is pressed
	do
	{
		setupButtonInterrupt();
		putToSleep();
                    
		// on wake, wait and check again to debounce
		delayMs(15);
	} while (!isPinHigh(BUTTON_1) && isPinHigh(BUTTON_2));

	sendButtonStatus();

	if (!isPinHigh(BUTTON_1) && !isPinHigh(BUTTON_2)) {
		// Interrupt was caused by pushing button 2 but not releasing button 1
		handleBothButtons();
	}
	else if (isPinHigh(BUTTON_1) && !isPinHigh(BUTTON_2)) {
		// Some how the timing worked out that you released button 1, but also pushed button 2.
		handleButton2Release();
	}
}

void handleButton2Release()
{
	//Button 2 has been pressed. Sleep until it is released or button 1 is pressed
	do
	{
		setupButtonInterrupt();
		putToSleep();
                    
		// on wake, wait and check again to debounce
		delayMs(15);
	} while (isPinHigh(BUTTON_1) && !isPinHigh(BUTTON_2));

	sendButtonStatus();

	if (!isPinHigh(BUTTON_1) && !isPinHigh(BUTTON_2)) {
		// Interrupt was caused by pushing button 2 but not releasing button 1
		handleBothButtons();
	}
	else if (!isPinHigh(BUTTON_1) && isPinHigh(BUTTON_2)) {
		// Some how the timing worked out that you released button 2, but also pushed button 1.
		handleButton1Release();
	}
}

// State when both buttons are pressed
void handleBothButtons()
{
	//Both buttons are currently pressed. Sleep until 1 is released
	do
	{
		setupButtonInterrupt();
		putToSleep();
                    
		// on wake, wait and check again to debounce
		delayMs(15);
	} while (!isPinHigh(BUTTON_1) && !isPinHigh(BUTTON_2));

	sendButtonStatus();

	if (isPinHigh(BUTTON_1) && isPinHigh(BUTTON_2)) {
		// somehow release both buttons simultaniously
		return;
	}
	else if (isPinHigh(BUTTON_1)) {
		handleButton2Release();
	}
	else if (isPinHigh(BUTTON_2)) {
		handleButton1Release();
	}
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

		sleepInit();
		radioQueueInit();
            
        while (1)
        {
			boardService();

			do
            {
                setupButtonInterrupt();
                putToSleep();
                        
                // on wake, wait and check again to debounce
                delayMs(15);
            } while (isPinHigh(BUTTON_1) && isPinHigh(BUTTON_2));

			sendButtonStatus();
			
			if (!isPinHigh(BUTTON_1) && !isPinHigh(BUTTON_2)) {
				handleBothButtons();
			}
			else if (!isPinHigh(BUTTON_1)) //button 1 pressed to wakeup from sleep
			{
				handleButton1Release();
			}
			else if (!isPinHigh(BUTTON_2)) {
				handleButton2Release();
			}
		}
	}
}
