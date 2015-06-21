Smart Citizen Kit WiFi
======================

**BETA Firmware for the [RTX4100](http://www.rtx.dk/RTX41xx_Wi-Fi_modules-3921.aspx) WiFi Module for the Smart Citizen Kit 2.0**

##Documentation

**Complete documentation comming soon**

Current documentation available at the original thesis: 

[Analysis, Improvement, and Development of New Firmware for the Smart Citizen Kit Ambient Board](http://openaccess.uoc.edu/webapps/o2/bitstream/10609/40042/6/mcolombTFG0115memoria.pdf) by [Miguel Colom Barco](https://github.com/mcolom)

###SPI commands reference

In production mode, the firmware waits for commands in the SPI channel, executes them, and return back information using also SPI communication. The details of the SPI communication protocol used are given in Section 12.3.
The SPI interface allows to communicate the RTX4100 with the outside using 15 different commands. These commands are documented in this sec- tion. The command must be always initiated by the upper layer by sending a byte which identifies the command which must be executed.


The list of commands and the binary protocol is as follows.


####Command #1 (get status)
This command is used to poll the status of the RTX4100. It returns a byte, where each bit has the following meaning:


1. Bit #0 (LSB): 1 if the WiFi chip is associated and connected to the AP, and 0 otherwise.
2. Bit #1: 1 if the TCP connection has been established, and 0 otherwise.
3. Bit #2: 1 if new data has been received at the TCP stream, and 0 otherwise.
4. Bit #3: 1 if the WiFi chip is suspended, and 0 otherwise.


####Command #2 (DNS33 resolve)
It allows to resolve a DNS name. The protocol is as follows:


1. Read the size in bytes of the name to resolve.
2. Read the name to resolve.
3. Resolve the name using the DNS system and return it as a 32 bits unsigned word.
￼33Domain Name System.

####Command #3 (IP config)
It configures how the RTX4100 should obtain the IP address (static IP or by DHCP). If the IP address is static, it configures also the static IP address, subnet, and gateway. After executing this command, the upper layer can associate and connect to the AP with command #5.
The configuration is given as a byte stream. If the byte stream is empty (its size is zero), it assumes that the configuration is stored at the NVS and it is read from there. If the byte stream is not empty, it is parsed and processed.

####Command #4 (TCP start)
It receives the server IP address and TCP port and starts a TCP connection. This functions is asynchronous exists immediately and the upper layer can look at the global status (SPI command #1) in order to check when the connection has been established.

The protocol is:

1. Read 4 bytes (rsuint32) from the SPI channel. This rsuint32 type represents the IPv4 address of the server.
2. Read 2 bytes (rsuint16) from the SPI channel. This rsuint16 type represents the TCP port of the server.
3. Start the TCP connection asynchronously.

####Command #5 (associate and connect to the AP)
It associates and connects to the AP which was already configured with command #3. No SPI data transfer is needed in this command. After executing the command, the upper layer can poll the status of the system in order to know when the WiFi chip could associate with the AP.

####Command #6 (disassociate and disconnect from the AP)
It disassociates and disconnects the WiFi chip from the AP. The WiFi chip is assumed to have been already associated to an AP by using first command #5.

####Command #7 (setup AP)
It configures the AP which the RTX4100 must associate and connect with. The configuration is given as a stream of bytes which contain all the informa- tion needed. If the configuration stream is empty (its size is zero), it means that the configuration data has been already stored at the NVS and therefore it should be used. This frees the upper layer to store the configuration and pass it as an argument to the RTX4100 each time it needs to connect to the AP. The configuration is only specified once, and the rest of the times it is simply read from the NVS.


####Command #8 (close TCP connection)
It closes the already established TCP connection.

####Command #9 (TCP receive)
When new TCP data arrives, an internal event fires and event handler copies the new data to the TCP receive buffer (rx buffer). The number of bytes in the queue is stored at the global variable TCP Rx bufferLength by the event handler.
When the upper layer wants to read the arrived data it executes this function. It simply writes TCP Rx bufferLength bytes from rx buffer to the SPI.

####Command #10 (TCP send)
This command is used to send data to the TCP stream. The procedure is as follows:


1. Read a word of two bytes (rsuint16) with the number of bytes which it should send to the TCP stream.
2. Read that amount of bytes from the SPI channel. This data is copied to the tx buffer buffer.
3. Write the read data to the TCP stream.

####Command #11 (Wifi chip power on/off)
This is used to power on/off the WiFi chip. Note that if the WiFi chip is powered off and the powered on, the WiFi chip must be associated and connected to the AP again, and the IP configuration procedure must be performed again as well. Normally this should be avoided, since it takes several seconds.
It is provided only in the case where the SCK is powered by batteries and the charge is very low. In that case, the SCK can power off the WiFi chip and store the data in the SD card instead of transmitting it.
This command reads a byte from the upper layer using SPI. If the byte is equal to 0, it means poweroff. If it is 1, it means poweron.
Note that for a quick suspend and resume the commands which must be executed are commands #14 and #15, namely.

####Command #12 (Wifi set powersave profile)
It configures the WiFi chip powersave profile. This commands read a byte from the upper layer using SPI, and sets the powersave profile accordingly. The code is:

* 0: low power 
* 1: medium power
* 2: high power
* 3: maximum power

####Command #13 (Wifi set transmit power)
It reads a byte from the SPI and sets the wireless transmission power accord- ingly. It power level goes from 0 (minimum) to 18 (maximum). Of course, if the power level is low, the battery current drain is also low, but it shortens the minimum allowed distance to the AP.

####Command #14 (Wifi chip suspend)
This commands puts the Atheros AR4100 WiFi chip is suspend mode and the EFM32 microcontroller in EM2 (low-power mode) in order to save energy.
When the WiFi is suspended it can neither send nor receive information using the radio channel, but the energy consumption is minimal. In the case of SmartCitizen boards the suspend mode is strongly recommended, since the SCK only needs to transmit information at large time intervals (once per minute, or even more). It does not make sense to keep the WiFi activated, since it would be idle most of the time. It must be considered that even if the WiFi is idle, it needs to use the radio to receive WiFi beacon frames for synchronization.
If the WiFi chip is suspended for more that approximately 60 seconds it needs about one second to wake up and be ready for transmission again. The reason is that the synchrony is lost and therefore it needs to look for at least one complete beacon frame. Typically, these beacon frames are send by the AP once per 102.4ms. The tests performed during these project show that, in practice, it takes from approximately 0.5 to 1.0 seconds to be ready for transmission after the WiFi chip has been suspended, because it needs to wake up the WiFi chip, to receive one or more beacon frames. Also, the RTOS needs to receive the WiFi wakeup event from the queue.


The firmware presented in this project does not only put the Atheros AR4100 WiFi chip in suspend mode, but also the EFM32 microcontroller in the RTX4100, in order to save even more energy. There are four energy modes: EM1, EM2, EM3, and EM4, where EM1 is the full-power mode and EM4 means that the microcontroller is totally stopped and it can be only woken up with an external reset signal. It was checked that mode EM3 can no be used directly with RTX4100, since the board reset when trying to wakeup. Mode EM2 works well with the proposed firmware and it allows to wake up the microcontroller by an external interrupt (when SPI data is received or a timer fires, among other events).


In any case, the decision to suspend the WiFi chip with this command, or to simply leave it in a low energy operation mode (with command #12) depends on the application. For the SmartCitizen boards, the recommended procedure is the following:

1. Suspend the whole SCK (base and RTX4100, with this command) for a given time period (for example, one minute or more), to save energy.
2. Resume the RTX4100 operation with command #15.
3. Read the sensors and send the data to the SmartCitizen platform.
4. Go to step 1.

####Command #15 (Wifi chip resume)
This command is used to put the suspended (with command #14) Atheros AR4100 WiFi in normal operation mode. It might take up to a second to resume, depending on the AP beacon interval, the Atheros chip reactivation, and the RTOS message handling. This function does not need to make the EFM32 microcontroller get out of the suspend mode, since this is done automatically when an external interrupt is detected. When this command is executed, the EFM32 will automatically get out of the suspend mode because of activity in the SPI channel.


##Authors

[Miguel Colom Barco](https://github.com/mcolom)

