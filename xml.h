#ifndef _XML_H
#define _XML_H



#define XML_ELEM_START   (1)  // For example: <user>  
#define XML_ELEM_END     (2)  // For example: </user>
#define XML_LABEL        (3)  // For example: <name>value</name>

int xml_add_elem(int flag, const char *elem, const char *val, char *buf);
int xml_add_header(char *buf);
int xml_add_end(char *buf);







#endif
