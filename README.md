
```
  ___    ___     ___ ___
 /'___\ / __`\ /' __` __`\
/\ \__//\ \_\ \/\ \/\ \/\ \
\ \____\ \____/\ \_\ \_\ \_\
 \/____/\/___/  \/_/\/_/\/_/
```
When minicom or picocom is too much - just use _com_.

## About
The initial source code was borrowed from [Ivan Tikhonov](http://brokestream.com/tinyserial.html).

Since I liked it so much, I decided to:
 - rewrite it to use epoll interface
 - add more baud rates
 - add ability to send break signal
 - do some cosmetic changes

