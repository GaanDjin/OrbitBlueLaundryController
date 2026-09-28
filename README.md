# Orbit Blue Laundry Controller

## First: This project involves Fire, Moving Parts, and Mains Electricity. This can result in burning the place down, being maimed, or death so don't attempt unless you know what you are doing!

### What this is:
An ESP32 firmware designed to talk to a server laundry card tap system (Currently closed source).
RFID tap → REST deduct balance and start machine.
Three profiles divided into 2 projects at the moment: kiosk for checking, loading, and refunding card balances and then the washer/dryer which deduct card balances and control machines.
OTA updates
And remote config via heartbeat.


I took over a small laundromat in the middle of nowhere Nov. 2024 and at the time only about 50% of the machines worked which meant lots of repairing. After I had them *mostly* up and running it became obvious the coin acceptors were a bit of a nightmare to use and watching people struggle to work out how coins worked (The number of time I had to tell someone which slot to put the Loonie in makes me want to cry) I decided I needed to fix that. After looking at some of the commercial products out there, which would have been nice, but were just way too expensive; I decided to build my own, from the ground up. 

Enter: Orbit Blue a Laundry card management system. This would be the hardware end of the system.

<img src="Images/dryerfrontpanel.jpg" style="width:300px; height:auto;"> 

[Dryer Start](Images/20260927_091349%20[x264].mp4)

It's hard to see in the video but as I hold the card up to the reader it starts by adding 5 minutes and then adds another 5 minutes. As timer mode is cumulative so every tap (or the longer you hold) will continue to add time up to your balance.

[Dryer Countdown](Images/20260927_091349%20[x264].mp4)

[Kiosk](Images/balancechecker.mp4)

The balance checker kiosk is what I use to load and refund customer cards and guest cards (Blue fob in the video).

The project is broken into a few parts:

The project files for the esp32 micro controllers written in C++ using Visual Studio Code.
3D files for the dryer boxes. Made to fit a Huebsch 30EG Dryer circa 1960s. Made in Blender.
PCB board to interface the different parts. Made in EasyEDA.

The old dryer coin drop which would accept quarters and rotate a gear and ratchet to push two switches and activate the dryer:
<img src="Images/coindropfront.jpg" style="width:300px; height:auto;">
<img src="Images/coindroptop.jpg" style="width:300px; height:auto;">
<img src="Images/coindropbottom.jpg" style="width:300px; height:auto;"><br />
So they've been $0.25 per 5 min for 60 years. (Or the really cheap that figured out a Nickel would usually work too)

I tried to make the system as modular as possible so that any one module can fail and be easily replaced without tossing the whole works. I did manage to blow up a relay module putting the front panel back on and it shorted to ground. *Ka-POW* and a puff of smoke. So, again, *LINE VOLTAGE... SCARY*

A Partially assembled dryer box:<br />
<img src="Images/dryercontroller.jpg" style="width:300px; height:auto;">
<img src="Images/PCBInCaseV1.jpg" style="width:300px; height:auto;">

I have yet to make a box for the washer:<br />
<img src="Images/controllerinwasher.jpg" style="width:300px; height:auto;">

What the initial test looked like:<br />
<img src="Images/WiringPrePCB.jpg" style="width:300px; height:auto;">

The PCB just have to snip the resistor leads:<br />
<img src="Images/PCBTop.jpg" style="width:300px; height:auto;">
<img src="Images/PCBUnderside.jpg" style="width:300px; height:auto;">




BOM:

ASA Filament for the Dryer boxes. PLA won't cut it as the dryer heat will warp it. <br />
1 x ESP32 Dev board. Specifically: 30PIN ESP32S ESP-WROOM-32 I use [this one](https://www.amazon.ca/dp/B0BQJ8BTVB?th=1)<br />
1 x 5mm LED <-- For Washer as a status indicator<br />
1 x 100k resistor <-- Washer<br />
1 x 480 Ohm resistor <-- Washer<br />
1 x 100 nF Capacitor (#104) <-- Optional to smooth out RFID Power<br />
1 x 10 uF Capacitor <-- Optional to smooth out RFID Power<br />
1 x [6mm High Side Knob 6 Pin 2 Position DPDT Slide Switch](https://www.amazon.ca/20Pcs-50VAC-Position-Switch-SK22H03/dp/B0BKG6Y4KM) <-- Not the ones I went with as I had a bunch laying around but something similar for on/off.<br />
1 x [IRM-05-5 AC to 5v DC](https://www.digikey.ca/en/products/detail/mean-well-usa-inc/IRM-05-5/7704652) <-- A cheap buck converter isn't the greatest because of the EMI the dryers create. Needed if you can't find a 5v supply.<br />
1 x [AQY212EH Solid State Photo-Coupled Relay (Photorelay)](https://www.digikey.ca/en/products/detail/panasonic-industry/AQY212EH/512405) <-- Photo Relay for Washer<br />
1 x [Two Channel Relay Module](https://www.aliexpress.com/item/10000000669335.html?spm=a2g0o.order_list.order_list_main.147.36d618020lMnqG) <-- OR Two Channel Relay for Dryer<br />
1 x [RC522 RFID Module](https://www.digikey.ca/en/products/detail/sunfounder/CN0090/18668629) <-- *Be very wary of cheap knockoffs on Amazon and AliExpress I ended up scrapping 25 of them* Even the official ones I have to reset them every minute because they have a tendency to stop working. <br />
2 x [15-pin connection header](https://www.digikey.ca/en/products/detail/sullins-connector-solutions/PPTC151LFBN-RC/810153) <-- To mount the esp32 to the PCB<br />
1 x [14-pin connection header](https://www.digikey.ca/en/products/detail/sullins-connector-solutions/PPTC141LFBN-RC/810152) <-- To connect the LCD to the PCB<br />
1 x [8-pin connection header](https://www.digikey.ca/en/products/detail/sullins-connector-solutions/PPTC081LFBN-RC/810147) <-- To Connect the RFID to the PCB<br />
3 x [4-pin connection header](https://www.digikey.ca/en/products/detail/sullins-connector-solutions/PPTC041LFBN-RC/810144)<br />
1 x [2-pin block terminal 2.54MM PCB](https://www.digikey.ca/en/products/detail/phoenix-contact/1725656/267462)<br />
1 x [4PIN Male for PC Computer ATX CPU Power Connector](https://www.aliexpress.com/item/32607655535.html?spm=a2g0o.order_list.order_list_main.50.36d618020lMnqG)<br />
1 x [4 inch LCD Display](https://www.aliexpress.com/item/1005005787550807.html?spm=a2g0o.order_list.order_list_main.65.36d618020lMnqG)<br />
Dupont Wire Male to Female:<br />
8-pins for RFID<br />
4-pins for Relay<br />
I couldn't find a source of just male to female so went with [ELEGOO 120pcs 20cm Multicolored Dupont Wire](https://www.amazon.ca/dp/B01EV70C78?th=1) or [RGBZONE 120pcs 20CM Multicolored Dupont](https://www.amazon.ca/dp/B01M1IEUAF?th=1)<br />
Also used them as wires for the switches<br />


<img src="Images/dryerbox.jpg" style="width:300px; height:auto;">

#Endpoints used:

Currently the API server I am using Orbit Blue server is closed source, But these are the endpoints the firmware uses and what they expect. Written in C#/.NET.

## POST "/Login"
Logs into the server with the username and password of the machine and then stores the bearer token data to refresh its login. 

Body: 
```
String body = "{\"username\":\"" + username + "\",\"password\":\"" + password + "\"}";
```
Response:
```
{
"accessToken": string,
"refreshToken": string,
"accessExpiresUtc": Iso8601,
"refreshExpiresUtc": Iso8601
}
```
---
## POST "/refresh"
Renew the login bearer token.

Body:
```
String body = "\" + _refreshToken + "\"";
```
Response:
```
{
"accessToken": string,
"refreshToken": string,
"accessExpiresUtc": Iso8601,
"refreshExpiresUtc": Iso8601
}
```

Iso8601 Format: YYYY-MM-DDTHH:MM:SS
Example: 2026-02-28T20:43:46.8152824+00:00

---
## POST /machines/heartbeat
Send a status update to the server and gets a command from the server if there is one. 

Body:
```
switch (status) {
  case MachineStatus::Running:    statusStr = "Running";    break;
  case MachineStatus::Error:      statusStr = "Error";      break;
  case MachineStatus::OutOfOrder: statusStr = "OutOfOrder"; break;
  default:                        statusStr = "Idle";       break;
}

String body = {"status": status}
```
Response:
```
{
  "command": command string,
  "commandPayload", ""
}

Command String can be one of:

  "" <-- Empty no orders.
  "TryAgain"
  "AddTime" <-- commandPayload will be an int counting the number of seconds to add.
  "Cancel"
  "Reboot"
  "OutOfOrder"
  "InOrder"
  "UpdateFirmware" <-- commandPayload will be either empty or the profile to download "kiosk", "washer", or "dryer". If empty then the firmware uses whatever FIRMWARE_PROFILE was set to during initial flash.
  "UpdateConfig" <-- commandPayload will be a json object containing the controllers config:

{
  "timerMode" : bool,    <-- True: The controller should hold the relay open for cycleLengthSeconds. This is cumulative. False: the controller will pulse out to the machine start pin.
  "cycleLengthSeconds : ulong, <-- How long each tap should run for when in timer mode.
  "amount" : double, <-- How much each tap costs.
  "coinMode" : bool, <-- Coin mode determines if the controller should pulse out once for machine start or pulse repeatedly to simulate a coin drop signal. 
  "coinCount" : int, <-- The number of coin pulses to send in coin mode.
  "coinPulseDuration" : int, <-- How long the machine start and coin pulses should stay "on"
  "coinPulseDelay" : int, <-- How long to wait between coin drop pulses 
  useMachineBusy" : bool, <-- If the machine has a machine busy pin use it to update status with running and ignore taps until the machine is finished.
  "busyCooldownSeconds" : int, <-- If the machine does not have a busy pin then this is how long the controller will issue a status of busy and ignore further taps. Gives a good estimate of when a washer is busy but has no status pin.
  "screenTimeout" : int, <-- How long the screen should stay bright before dimming. 
  "timeRemaining" : int <-- When in timer mode if the esp32 controller has rebooted this can be used to tell the controller how long it has left on the current cycle.
}

```

<img src="Images/washercardreader.jpg" style="width:300px; height:auto;">

---
## POST /tap/deduct
Deduct / remove funds from a customer account given the card id.

Body:
```
String body = {"cardId": "string",  <-- The Hex ID of the RFID card. Must match a card in the database.
    "accountId": "string", <-- Originally used to identify the machine sending the deduct. Now unused always Guid.Zero
    "amount": number, <-- The amount to deduct from that customer account.
    "processorName": "CASH" <-- Processor is kind of a hint to say how this transaction was performed. Currently the firmware is set to "Cash"
    };
```
Response:
```
{
"message": string, <-- A string message if there was a problem.
"newBalance": 0.00, <-- The new account balance
"accountDisplayName": string <-- the Display name of the customer account. Currently unused by the Firmware. 
}
```

---
## POST /tap/refund
Removes funds to the customer account given the card id. Can be marked as refunded on the server to differentiate from a machine using their funds.

Body:
```
{
    "cardId": string,  <-- The Hex ID of the RFID card. Must match a card in the database.
    "amount": number, <-- The amount to refund from that customer account.
    "processorName":"CASH" <-- Processor is kind of a hint to say how this transaction was performed. Currently the firmware is set to "Cash"
}
```
Response:
```
{
"message": string, <-- A string message if there was a problem.
"newBalance": 0.00, <-- The new account balance
"accountDisplayName": string <-- the Display name of the customer account. Currently unused by the Firmware. 
}
```

---
## POST /tap/add
Add funds to the customer account given the card id.

Body:
```
{
    "cardId": string,  <-- The Hex ID of the RFID card. Must match a card in the database.
    "amount": number, <-- The amount to add to that customer account.
    "processorName": "CASH" <-- Processor is kind of a hint to say how this transaction was performed. Currently the firmware is set to "Cash"
}
```
Response:
```
{
"message": string, <-- A string message if there was a problem.
"newBalance": 0.00, <-- The new account balance
"accountDisplayName": string <-- the Display name of the customer account. Currently unused by the Firmware. 
}
```

<img src="Images/washercardreader2.jpg" style="width:300px; height:auto;">

---
## GET "/cards/list?Filters[CardId]=%cardId%"
Used to check if the card being tapped is an admin card. The response can have lots of other details but this is the only field we care about.

Response:
```
{
  rows: [
    {
      isAdminCard: bool
    } 
  ]
}
```

---
## GET "/time"
Currently unused.

---
## GET "/tap/%cardId%/balance"
```
Where %cardId% is the Hex ID of the RFID card
```
Response:
```
{
  "balance": 0.00 <-- The current balance of the customer account for that card.
}
```

---
## GET "/firmware/version?profile=" + profile
Checks to see if the server has a new firmware for an over the air update. 

Response:
```
{
  version: string <-- The version number for example "1.0.17" of the latest version on the server. If the value is different (not necessarily higher) then download the version from the server.
}
```

---
## GET "/firmware/download?profile=" + profile
Download the new firmware from the server and perform an update. As HTTPUpdate on ESP32 Arduino core doesn't support authentication this must be \[AllowAnonymous\].

Response:
```
File: application/octet-stream
```

<img src="Images/outoforder.jpg" style="width:300px; height:auto;">

There was a bit of trial and error getting the right components, learning about counterfeit electronics, and iterating through what works and what works better.
But, it has been a great project and tons of fun to go through the whole process learning how the machines work and how to interface with them. 

Hopefully, you might find this as a useful starting point for your own hardware projects. Happy building!
