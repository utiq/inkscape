#!/usr/bin/env python3

from xml.dom import minidom
import sys


sys.stdout.write("char * stringlst = [")

for rawdoc in sys.argv[1:]:
    doc = minidom.parse(rawdoc)
    filters = doc.getElementsByTagName('pattern')

    for filter in filters:
        stockid = filter.getAttribute('inkscape:stockid')
        if stockid == "":
            stockid = filter.getAttribute('inkscape:label')
        if stockid != "":
            sys.stdout.write("N_(\"" + stockid + "\"),")

sys.stdout.write("];")
