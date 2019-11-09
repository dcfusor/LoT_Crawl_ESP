# LoT_Crawl_ESP
A leaf node in my LAN of Things, ESP8266-based, for my living quarters crawl space

This is some code for an ESP8266-based leaf node in my LAN of things.  It's doubtful anyone will use
this software "As is", since it has a lot of stuff specific to my needs here.  However, it's chock full
of "steal me" examples on how to do things in this kind of setup, so I thought I'd share.

![Hardware](https://github.com/dcfusor/LoT_Crawl_ESP/blob/master/OTLoTOTA/OTLoT.JPG)

I implemented a little scheduler (too simple to call an opsys) for this, so as to spread out the load and
not do too much on a pass through loop().  The ESP WiFi stack doesn't like that, and here ther's no need
anyway.  This lets me schedule things at fixed intervals - perhpas many at once, but get them executed in order
of priority in subsequent passes through loop9), which yeilds often enough to keep the WiFi stack happy.

This implements my LAN "DNS", and broadcasts a name/ip pair to anyone listening on UDP port 53831 on the LAN.
This helps my setups come right up after a power glitch, for example, as it's quicker than the usual stuff.

THis also implements the over-the-air update mechanism, though I've not used it much.  The target machine is in
a very cramped crawl space with nice steel beams to dent your head with, wires and pipes to get caught on or
do damage to...and so on - it's better if you can not have to go there.

This measures indoor, outdoor, and crawl space temeprature and humidity - the indoor and outdoor use DHT22 sensors, and
the crawl space one uses a BMP 280, which also has a barometer.  Since my cistern is right there, the crawl space
tempterature becomes real important on super cold days - IE, it's nice to know if your plumbing might freeze so you
can plug in a heating element in that cistern.

It also measures water level in the cistern, and controls 2 valves in the line from the rain collection system.  One
allows water into the cistern, the other allows the collection system to drain outdoors - first to a spare barrel I use
for water for the Maytag wringer, and then an overflow on that if there's too much.
Another node in the system has a video camera and a controllable light at the bottom of the collection tank so as to asses
the turbidity of the raw water to decide whether to keep it or ditch it - it doesn't have to be that great to just wash
clothes.

There is a little OLED display to help troubleshoot should one just have to go down there to perhaps fix something.
This system also uses 12 volts for the valve actuators, and for the water level measurement circuit.  I created a temperature
compensated regulator to keep a little 12v 7 ah battery up, and the rest runs from that as a little UPS system.

(There's a lot more to put in this README...I'll get there)
