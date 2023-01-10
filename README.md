# viserve
A freely configurable REST server supporting the Viessmann Optolink interface. 

In order to not overload the slow optical interface the service caches all exposed parameters. 

The software provides a sample configuration to display information of a 20CB type. 
Note that addresses seem to vary between installations. Hence use write operations only after validation of the read interface.

# Dependencies
* libmicrohttpd - The project uses libmicrohttpd for serving files and the REST API.
* pugixml - Used for parsing the configuration xml file
* jquery - Only needed when making use of the animated svg html page.

# Build instructions
A Makefile supports building as daemon for Linux. Additionally a Visual Studio project file is provided for Windows.

Linux prerequisit:
* apt-get install libmicrohttpd-dev 
* apt-get install libmicrohttpd12

For Windows copy binaries and include file into directory src

# Configuration
The startup file config.xml consists of two main sections 'service' and 'api'.

## Service
The service section allows to configure the service endpoint, the location of files to be served as well the logging configuration.

Logging levels available:
 > 1 Log read and write commands with address and hex payload
 > 3 Log hex communication on the wire
 
 ## API
 
All xml nodes beyond API are directly served as path. An xml node may either have children or is a leaf node. Leaf nodes need an address, a type and optionally an operation definition.

The following example exposes the path /api/status with subnodes. Boiler is a decimal fixed point value to be read via address 0802, while circulation is a boolean value to be read at address 6515. Additionally the refresh rate for the circulation pump is set to two seconds to provide a more responsive interface.
```
<api>
  <status>
    <temperature>
      <boiler type='decimal' addr='0802'/>
    </temperature>
    <pump>
      <circulation type='bool' addr='6515' refresh='2'/>
    </pump>
  </status>
</api>
```
A client may choose request a single parameter ```/api/status/temperature/boiler``` or request a set of information via ```/api/status```.

The following attributes allow to map interface nodes to heating system parameters:

1. type
  * decimal (1/10 fixed point)
  * centi (1/100 fixed point)
  * milli (1/1000 fixed point)
  * int (integer)
  * half (1/2 fixed point)
  * hex (hexadecimal)
  * bool (boolean)

2. len [byte]
By default all parameters but boolean and half are assuemed to be of two byte length. With the optional attribute len the default can be overwritten.

3. operation
By default all parameters are assumed to be readonly. Set operation to 'rw' for read/write, 'w' for write only and 'p' for pulse.
Pulse needs a further 'duration' attribute to define the duration of the pulse in seconds.

4. refresh [seconds]
The server default cache refresh rate can be individually adjusted for commands. Thermal parameters are typically changing slowly allowing for longer caching while boolean value may need to be adapted quicker in order to provide feedback.

# References
Thanks to the indepth engineering efforts of the following projects:

* [vcontrold](https://github.com/openv/vcontrold)
* [Interfacing Vitovalor 300-P with a Raspberry Pi ](https://projects.webvoss.de/2017/11/05/interfacing-vitovalor-300-p-with-a-raspberry-pi/)
* [vitalk](https://github.com/klauweg/vitalk)

# Todo

* https configuration
* authentication
* MQTT publisher
