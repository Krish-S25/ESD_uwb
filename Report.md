SYSTEM ARCHITECTURE

This project describes a high precision indoor positioning system that utilizes ultra wideband technology to achieve centimeter level tracking accuracy. The system is designed to provide location data in environments where traditional GPS signals are unavailable or unreliable. It consists of stationary anchors positioned at fixed coordinates and mobile tags that are attached to the assets being tracked. The core of the tag is an ESP32 WROOM 32 microcontroller paired with a BU03 radio module which uses nanosecond pulses to measure distance through time of flight calculations.

POWER MANAGEMENT

The power circuit is designed to handle charging and regulation for portable operation using a lithium polymer battery. A USB C port serves as the primary power input and uses detection resistors to ensure a five volt supply from modern chargers. The charging process is managed by an MCP73831 integrated circuit which is configured to charge the battery at a safe rate of two hundred milliamperes. For system stability an MCP1700 low dropout regulator converts the fluctuating battery voltage into a constant three point three volt supply. A physical slide switch is placed in the circuit to allow the user to turn the tracking logic off while still allowing the battery to charge.

DATA LINE MAPPING 
<img width="801" height="461" alt="image" src="https://github.com/user-attachments/assets/d3ca8bdc-624f-4add-b68e-ce201a4117c3" />
The communication between the microcontroller and the radio module is handled through a high speed serial peripheral interface. Following the recommended application guidance the system uses specific pin assignments for reliable data transfer.

NOISE REDUCTION

Maintaining signal integrity is a priority because the radio pulses operate at a very high frequency of six point five gigahertz. The design implements a specific capacitor strategy to filter out electrical interference from the digital components. A large one hundred microfarad capacitor provides a reservoir of energy for sudden power demands while smaller four point seven and zero point two two microfarad capacitors target medium and high frequency noise respectively. These components work together as a low pass filter to ensure that the sensitive timing measurements of the radio module are not disrupted by power fluctuations.

DESIGN CONSIDERATIONS

The physical layout of the circuit board is critical for the radio performance and accuracy of the system. The most important requirement is the creation of a keep out zone directly underneath the radio antenna where no copper or electrical traces are allowed. This prevents the board material from interfering with the radio waves and maximizing the effective range in large rooms. Furthermore all ground pins are connected to a solid copper plane to provide a clean return path for electricity and the decoupling capacitors are placed as close as possible to the power pins to maintain stability.
