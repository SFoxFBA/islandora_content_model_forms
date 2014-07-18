<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dtca="http://<IP ADDRESS>/schemas/dtca/1.0.0"
    version="1.0">
    <xsl:output method="xml" indent="yes"/>
    <xsl:template match="/">
        <xsl:element name="dc:title">
            <xsl:value-of select="/dtca:Dataset/dtca:title"/>
        </xsl:element>
    </xsl:template>
    <!-- suppress all else:-->
    <xsl:template match="*"/>
</xsl:stylesheet>

