# viserve
A freely configurable REST server supporting the Viessmann Optolink interface.

The software provides a sample configuration to display information of a 20CB type. 
Note that addresses seem to vary between installations. Hence use write operations only after validation of the read interface.

# Dependencies
* libmicrohttpd - The project uses libmicrohttpd for serving files and the REST API.
* pugixml - Used for parsing the configuration xml file
* jquery - Only needed when making use of the animated svg html page.

# References
Thanks to the indepth engineering efforts of the following projects:

* [vcontrold](https://github.com/openv/vcontrold)
* [Interfacing Vitovalor 300-P with a Raspberry Pi ](https://projects.webvoss.de/2017/11/05/interfacing-vitovalor-300-p-with-a-raspberry-pi/)
* [vitalk](https://github.com/klauweg/vitalk)

# Todo

* https configuration
* authentication
* MQTT publisher
