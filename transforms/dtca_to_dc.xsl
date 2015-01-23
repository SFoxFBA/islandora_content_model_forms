<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:dc="http://purl.org/dc/elements/1.1/" 
    		xmlns:dtca="http://<IP ADDRESS>/schemas/dtca/1.0.0"
                xmlns:oai_dc="http://www.openarchives.org/OAI/2.0/oai_dc/"
		xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
                version="1.0">
    <xsl:output method="xml" indent="yes"/>
    <xsl:template match="/">
        <oai_dc:dc xsi:schemaLocation="http://www.openarchives.org/OAI/2.0/oai_dc/ http://www.openarchives.org/OAI/2.0/oai_dc.xsd">
            <xsl:element name="dc:title">
                <xsl:value-of select="/dtca:Dataset/dtca:title"/>
            </xsl:element>
            <xsl:element name="dc:identifier">
                <xsl:value-of select="//dc:identifier"/>
            </xsl:element>
        </oai_dc:dc>
    </xsl:template>
    <!-- suppress all else:-->
    <xsl:template match="*"/>
</xsl:stylesheet>

