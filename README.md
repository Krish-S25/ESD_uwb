# ESD_uwb
ESD project Group 11 - Krishang Singh , Krishna Kumar Mahto , Harshul Tripathi 

Files in repository 
-schematics for anchor and tag systems
-pcb design for anchor sytem
-codes for anchors+tags and library "dw3000.h" (works for ESP32 and Bu03 combination) for using Aithinker Bu03 with ESP32 MCU using SPI

Procedure to replicate 
1. Form circuits as shown in schematics with exact pin mappings
2. Map the room that needs to be covered with accurate anchor coordinates (Higher and spread out is better)
3. Upload codes to all units (3 anchors and 1 tag)
4. Place all anchors in designated locations
5. Provide power and start all systems

Main components - Esp32 MCUs x 4 , Aithinker Bu03 x 4 , Voltage regulators (MCP1700 for tag and AMS1117 for anchors) x 4 , Battery powering system x 1


-------------------------------------------------------------------------------------------------------------------------------------------------------------------
More about the project..
This high-precision Indoor Positioning System (IPS) is designed to solve the "last meter" problem where GPS fails.

Core System ArchitectureThe system functions through the interaction of two distinct hardware roles: Anchors and Tags.
1. The Tag (Mobile Node)The Tag is the mobile device attached to the object you want to track.Brain: Powered by an ESP32-WROOM-32 for logic and wireless data transmission.Radio: Utilizes a BU03 UWB module to send and receive nanosecond-wide pulses.Power Management: Includes an integrated MCP73831 LiPo charger and an MCP1700 LDO regulator to provide a stable 3.3V rail from a 300mAh battery.Control: Features a physical slide switch for power and a status LED for charging feedback.
2. The Anchors (Reference Nodes)Anchors are stationary units mounted at known, fixed coordinates within a space.They act as the "satellites" of the indoor system.Anchors listen for "pings" from the Tag and respond with precise timing data.
3. How It All Comes Together....
The system determines location using Time of Flight (ToF) math rather than signal strength (RSSI), which is often unreliable indoors.
The Ping: The Tag sends a high-frequency UWB pulse.
The Response: Multiple Anchors receive the pulse and send an immediate acknowledgement.Trilateration: By knowing the distance from at least three different Anchors, the system uses trilateration to pinpoint the Tag's exact X,Ycoordinates (and optionally |Z| values with 3 anchors , when all anchors are at same height).

Why This System?  Unlike passive tracking solutions (like RFID stickers or barcodes), this active UWB system offers significant technical advantages:
Centimeter Precision: While standard Bluetooth or Wi-Fi tracking is accurate to within a few meters, this system achieves 10cm accuracy.
Multipath Immunity: UWB pulses are so short that the system can distinguish the direct signal from "echoes" bouncing off metal machinery or concrete walls.Active
Intelligence: Because the Tag is powered, it can report telemetry (like battery life) alongside its position.
