/** wireless_stylus_receiver app:

Receives signals from the wireless_stylus app and reports them to the
computer using its USB HID interface.

*/

#include <wixel.h>
#include <usb.h>
#include <usb_hid.h>
#include <radio_queue.h>

void updateLeds()
{
    usbShowStatusWithGreenLed();
}

void rxMouseState(void)
{
    uint8 XDATA * rxBuf;

    if (rxBuf = radioQueueRxCurrentPacket())
    {
        usbHidMouseInput.x = 0;
        usbHidMouseInput.y =  0;
        usbHidMouseInput.buttons = rxBuf[1];
        usbHidMouseInputUpdated = 1;
        radioQueueRxDoneWithPacket();
    }
}

void main()
{
    systemInit();
    usbInit();

    radioQueueInit();

    while(1)
    {
        updateLeds();
        boardService();
        usbHidService();

        rxMouseState();
    }
}
