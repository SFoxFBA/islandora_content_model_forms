<?xml version="1.0" encoding="UTF-8"?>
<!-- Author NBYWELL Date 20 Jan 2005                                                         -->
<!-- Transforms the XML of Activity, Dataset and DataComponent from the DTC namespace (dtca) -->
<!-- into Dublin Core (dc) as part of the publication process. It is primarily to provide    -->
<!-- the search terms for the Solr Search functionality and is not intended to provide a     -->
<!-- reliable Dublin Core metadata representation.                                           -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:dc="http://purl.org/dc/elements/1.1/" 
                xmlns:dtca="http://fba.org.uk/schemas/dtca/1.0.0"
                xmlns:oai_dc="http://www.openarchives.org/OAI/2.0/oai_dc/"
                xmlns:gco="http://www.isotc211.org/2005/gco"
		            xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
		            xmlns:gml="http://www.opengis.net/gml"
		            xmlns:mo="http://fba.org.uk/schemas/moles/3.4.0"
                version="1.0">
    <xsl:output method="xml" indent="yes"/>
    <xsl:template match="/">
      <oai_dc:dc xsi:schemaLocation="http://www.openarchives.org/OAI/2.0/oai_dc/ http://www.openarchives.org/OAI/2.0/oai_dc.xsd">
        <xsl:apply-templates select="/dtca:Activity"/>
        <xsl:apply-templates select="/dtca:Dataset"/>
        <xsl:apply-templates select="/dtca:DataComponent"/>        
      </oai_dc:dc>
    </xsl:template>
  
  <xsl:template match="/dtca:Activity">
    <!-- Process the metadata for the dtca datastream of an Activity Class object  -->
    <!-- The dtca:archivalRecordCreationDate field is only relevant to the private -->
    <!-- collection, not the published collection so no transformation for it, and -->
    <!-- there is no suitable field still available in DC for it anyway.           -->
    <xsl:apply-templates select="dtca:title"/>
    <xsl:apply-templates select="dc:identifier"/>
    <xsl:apply-templates select="dtca:abstract"/>
    <xsl:apply-templates select="dtca:keyword[@authority='fba subject']"/>
    <xsl:apply-templates select="dtca:keyword[@authority='fba geographic']"/>
    <xsl:apply-templates select="dtca:keyword[@authority='fba taxonomic']"/>
    <xsl:apply-templates select="dtca:relatedParty[@type='personal']"/>
    <xsl:apply-templates select="dtca:relatedParty[@type='corporate']"/>
    <xsl:call-template   name="additions"/>
    <xsl:apply-templates select="dtca:languageTerm[@authority='iso639-1']"/>
    <xsl:apply-templates select="mo:timeWindow"/>
    <xsl:apply-templates select="dtca:archivalPublicationVersion"/>
    <xsl:apply-templates select="dtca:archivalPublicationDate"/>
  </xsl:template>

  <xsl:template match="/dtca:Dataset">
    <!-- Process the metadata for the dtca datastream of an Dataset Class object -->
    <xsl:apply-templates select="dtca:title"/>
    <xsl:apply-templates select="dc:identifier"/>
    <xsl:apply-templates select="dtca:abstract"/>
    <xsl:apply-templates select="dtca:keyword[@authority='fba subject']"/>
    <xsl:apply-templates select="dtca:keyword[@authority='fba geographic']"/>
    <xsl:apply-templates select="dtca:keyword[@authority='fba taxonomic']"/>
    <xsl:apply-templates select="dtca:relatedParty[@type='personal']"/>
    <xsl:apply-templates select="dtca:relatedParty[@type='corporate']"/>
    <xsl:call-template   name="additions"/>
    <xsl:apply-templates select="dtca:languageTerm[@authority='iso639-1']"/>
    <xsl:apply-templates select="mo:phenomenonTime"/>
    <xsl:apply-templates select="dtca:archivalPublicationVersion"/>
    <xsl:apply-templates select="dtca:archivalPublicationDate"/>
  </xsl:template>

  <xsl:template match="/dtca:DataComponent">
    <!-- Process the metadata for the dtca datastream of an Data-Component Class object -->
    <xsl:apply-templates select="dtca:title"/>
    <xsl:apply-templates select="dc:identifier"/>
    <xsl:apply-templates select="dtca:abstract"/> <!-- Used in all object classes except for supplementary File datacomponents created via the media form-->
    <xsl:apply-templates select="dtca:description"/> <!-- Used instead of 'abstract' in the media form for a supplementary file data-component -->
    <xsl:apply-templates select="dtca:keyword[@authority='fba subject']"/>  
    <xsl:apply-templates select="dtca:keyword[@authority='fba taxonomic']"/>
    <xsl:apply-templates select="dtca:keyword[@authority='fba depicts taxon']"/> <!-- Only present for supplementary file datacomponents created via the media form -->
    <xsl:apply-templates select="mo:observedProperty"/> <!-- Only present for the core datacomponents --> 
    <xsl:apply-templates select="dtca:keyword[@authority='fba geographic']"/>
    <xsl:apply-templates select="dtca:relatedParty[@type='personal']"/>
    <xsl:apply-templates select="dtca:relatedParty[@type='corporate']"/>
    <xsl:call-template name="additions"/>
    <xsl:apply-templates select="dtca:languageTerm[@authority='iso639-1']"/>
    <xsl:apply-templates select="mo:phenomenonTime"/> <!-- Only present for the core datacomponents -->
    <xsl:apply-templates select="mo:timeWindow"/> <!-- Only present for supplementary file datacomponents created via the generic form -->
    <xsl:apply-templates select="dtca:originInfo"/> <!-- Only present for supplementary File datacomponents created via the media form -->   
    <xsl:apply-templates select="dtca:archivalPublicationVersion"/>
    <xsl:apply-templates select="dtca:archivalPublicationDate"/>
  </xsl:template>

  <xsl:template match="dtca:title">
    <xsl:variable name="title" select="normalize-space(text())"/> 
    <xsl:if test="$title">
      <dc:title>
        <xsl:value-of select="$title"/>
      </dc:title>
    </xsl:if>     
  </xsl:template>

  <xsl:template match="dc:identifier"> <!-- Caters for some under-the-bonnet Islandora processing that inserts the identifier -->
    <xsl:variable name="identifier" select="normalize-space(text())"/> 
    <xsl:if test="$identifier">
      <dc:identifier>
        <xsl:value-of select="$identifier"/>
      </dc:identifier>
    </xsl:if>     
  </xsl:template>
  
  <xsl:template match="dtca:abstract">
    <!-- This is output to dc.description because the non-sidora transforms output to dc.description -->
    <!-- rather than dc.abstract and as some websites may contain both Sidora and non-Sidora data    -->
    <!-- it means that all can be searched via one search-box field.                                 -->
    <xsl:variable name="abstract" select="normalize-space(text())"/>
    <xsl:if test="$abstract">
      <dc:description>
        <xsl:value-of select="$abstract"/>
      </dc:description>
    </xsl:if>       
  </xsl:template>

  <xsl:template match="dtca:description">
    <xsl:variable name="description" select="normalize-space(gco:CharacterString)"/>
    <xsl:if test="$description">
      <dc:description>
        <xsl:value-of select="$description"/>
      </dc:description>
    </xsl:if>       
  </xsl:template>

  <xsl:template match="dtca:keyword[@authority='fba subject']">
    <xsl:variable name="subjectCharacterString" select="normalize-space(gco:CharacterString)"/>
    <xsl:if test="$subjectCharacterString">
      <dc:subject>
        <xsl:value-of select="$subjectCharacterString"/>
      </dc:subject>
    </xsl:if>        
  </xsl:template>
  
  <xsl:template match="dtca:keyword[@authority='fba taxonomic']">
    <xsl:variable name="taxonomicCharacterString" select="normalize-space(gco:CharacterString)"/>
    <xsl:if test="$taxonomicCharacterString">
      <dc:subject>
        <xsl:value-of select="$taxonomicCharacterString"/>
      </dc:subject>
    </xsl:if>    
  </xsl:template>

  <xsl:template match="dtca:keyword[@authority='fba depicts taxon']">
    <xsl:variable name="depictsTaxonCharacterString" select="normalize-space(gco:CharacterString)"/>
    <xsl:if test="$depictsTaxonCharacterString">
      <dc:subject>
        <xsl:value-of select="$depictsTaxonCharacterString"/>
      </dc:subject>
    </xsl:if>  
  </xsl:template>
  
  <xsl:template match="mo:observedProperty">
    <xsl:variable name="observedProperty" select="normalize-space(gco:CharacterString)"/>
    <xsl:if test="$observedProperty"> 
      <dc:subject>
        <xsl:value-of select="$observedProperty"/>
      </dc:subject>
    </xsl:if>  
  </xsl:template> 
  
  <xsl:template match="dtca:keyword[@authority='fba geographic']">
    <xsl:variable name="geographicCharacterString" select="normalize-space(gco:CharacterString)"/>
    <xsl:if test="$geographicCharacterString">
      <dc:coverage>
        <xsl:value-of select="$geographicCharacterString"/>
      </dc:coverage>
    </xsl:if>  
  </xsl:template>

  <xsl:template match="dtca:relatedParty[@type='personal']">
    <xsl:variable name="familyNamePart" select="normalize-space(dtca:namePart[@type='family'])"/>    
    <xsl:if test="$familyNamePart">
      <dc:contributor>
        <xsl:value-of select="$familyNamePart"/><xsl:text>, </xsl:text><xsl:value-of select="dtca:namePart[@type='given']"/>
      </dc:contributor>
    </xsl:if>  
  </xsl:template>

  <xsl:template match="dtca:relatedParty[@type='corporate']">
    <xsl:variable name="namePart" select="normalize-space(dtca:namePart)"/> 
    <xsl:if test="$namePart">  
      <dc:contributor>
        <xsl:value-of select="$namePart"/>
      </dc:contributor>
    </xsl:if>  
  </xsl:template>

  <xsl:template match="dtca:languageTerm[@authority='iso639-1']">
    <xsl:variable name="languageTerm" select="normalize-space(text())"/>
    <xsl:if test="$languageTerm"> 
      <dc:language>
        <xsl:value-of select="$languageTerm"/>
      </dc:language>
    </xsl:if>  
  </xsl:template>

  <xsl:template match="mo:phenomenonTime">
    <xsl:variable name="beginPosition" select="gml:TimePeriod[@gml:id='tp1']/gml:beginPosition"/>
    <xsl:variable name="endPosition" select="gml:TimePeriod[@gml:id='tp1']/gml:endPosition"/>
    <xsl:choose>
      <xsl:when test="normalize-space($beginPosition)">
        <dc:date>
          <xsl:call-template name="formatDate">
            <xsl:with-param name="unformattedDate" select="$beginPosition"/>
            </xsl:call-template>
        </dc:date>
      </xsl:when>
      <xsl:otherwise>      
        <xsl:if test="normalize-space($endPosition)">
          <dc:date>
            <xsl:call-template name="formatDate">
              <xsl:with-param name="unformattedDate" select="$endPosition"/>
            </xsl:call-template>
          </dc:date>
        </xsl:if>
      </xsl:otherwise>
    </xsl:choose> 
  </xsl:template>

  <xsl:template match="mo:timeWindow">
    <xsl:variable name="beginPosition" select="gml:TimePeriod[@gml:id='tp1']/gml:beginPosition"/>
    <xsl:variable name="endPosition" select="gml:TimePeriod[@gml:id='tp1']/gml:endPosition"/>
    <xsl:choose>
      <xsl:when test="normalize-space($beginPosition)">
        <dc:date>
          <xsl:call-template name="formatDate">
            <xsl:with-param name="unformattedDate" select="$beginPosition"/>
          </xsl:call-template>
        </dc:date>
      </xsl:when>
      <xsl:otherwise>      
        <xsl:if test="normalize-space($endPosition)">
          <dc:date>
            <xsl:call-template name="formatDate">
              <xsl:with-param name="unformattedDate" select="$endPosition"/>
            </xsl:call-template>
          </dc:date>
        </xsl:if>
      </xsl:otherwise>
    </xsl:choose> 
  </xsl:template>

  <xsl:template match="dtca:originInfo">
    <xsl:variable name="dateCaptured" select="normalize-space(dtca:dateCaptured)"/>
    <xsl:if test="$dateCaptured">
      <dc:date>
        <xsl:call-template name="formatDate">
          <xsl:with-param name="unformattedDate" select="$dateCaptured"/>
        </xsl:call-template>
      </dc:date>
    </xsl:if> 
  </xsl:template>

  <xsl:template match="dtca:archivalPublicationDate">
    <xsl:if test="normalize-space(text())">
      <!-- Putting this in the 'dc.create' field is a distortion -->
      <!-- of Dublin Core because this field is supposed to      -->
      <!-- describe the creation date of the resource rather     -->
      <!-- than the creation date of the metadata record,        -->
      <!-- but to be consistent with the non-sidora processing   -->
      <!-- that date is put the 'dc.date' field. Date range      -->
      <!-- searching of publication date may be required at      -->
      <!-- some point so the dc.create field is used as there    -->
      <!-- is no other alternative.                              -->
      <dc:create>
        <xsl:call-template name="formatDate">
          <xsl:with-param name="unformattedDate" select="text()"/>
        </xsl:call-template>
      </dc:create>
    </xsl:if> 
  </xsl:template>

  <xsl:template match="dtca:archivalPublicationVersion">
    <xsl:if test="normalize-space(text())"> 
      <dc:hasVersion>
        <xsl:value-of select="text()"/>
      </dc:hasVersion>
    </xsl:if> 
  </xsl:template>

  <xsl:template name="additions">
    <dc:publisher>Freshwater Biological Association</dc:publisher>
  </xsl:template>
  
  <xsl:template name="formatDate">
    <!-- Validate and reformat the date so that the resulting string is in             -->
    <!-- Solr-friendly format for date range searching i.e. YYYY-MM-DDTHH:MM:SS.MMMZ   -->
    <!-- The expected input format is YYYY-MM-DD HH:MM:SS although it may              -->
    <!-- have only been partially completed in some instances, so the remainder        -->
    <!-- is appended with zeroed values when necessary. Milliseconds will not          -->
    <!-- normally have been entered by the user but if they have been they are         -->
    <!-- carried through into the reformatted date.                                    -->
    <xsl:param name="unformattedDate" />    
    <xsl:variable name="year" select="substring($unformattedDate,1,4)"/>
    <xsl:variable name="month" select="substring($unformattedDate,6,2)"/>
    <xsl:variable name="day" select="substring($unformattedDate,9,2)"/>
    <xsl:variable name="hour" select="substring($unformattedDate,12,2)"/>
    <xsl:variable name="minute" select="substring($unformattedDate,15,2)"/>
    <xsl:variable name="second" select="substring($unformattedDate,18,2)"/>
    <xsl:variable name="millisecond" select="substring($unformattedDate,21,3)"/>
    <xsl:choose>
       <xsl:when test="(number($year)) and (string-length($year) = 4)">      
         <xsl:value-of select="$year"/><xsl:text>-</xsl:text>
         <xsl:choose>            
           <xsl:when test="(number($month)) and (string-length($month) = 2)">
             <xsl:value-of select="$month"/><xsl:text>-</xsl:text>
             <xsl:choose>            
               <xsl:when test="(number($day)) and (string-length($day) = 2)">
                 <xsl:value-of select="$day"/><xsl:text>T</xsl:text>                 
                 <xsl:choose>            
                   <xsl:when test="(number($hour)) and (string-length($hour) = 2)">
                     <xsl:value-of select="$hour"/><xsl:text>:</xsl:text>                    
                     <xsl:choose>            
                       <xsl:when test="(number($minute)) and (string-length($minute) = 2)">
                         <xsl:value-of select="$minute"/><xsl:text>:</xsl:text>                                            
                         <xsl:choose>            
                           <xsl:when test="(number($second)) and (string-length($second) = 2)">
                             <xsl:value-of select="$second"/><xsl:text>.</xsl:text>                             
                             <xsl:choose>            
                               <xsl:when test="(number($millisecond)) and (string-length($millisecond) = 3)">
                                 <xsl:value-of select="$millisecond"/><xsl:text>Z</xsl:text>
                               </xsl:when>          
                               <xsl:otherwise>
                                 <xsl:value-of select="'000Z'"/>
                               </xsl:otherwise>
                             </xsl:choose>                                                             
                           </xsl:when>          
                           <xsl:otherwise>
                             <xsl:value-of select="'00.000Z'"/>
                           </xsl:otherwise>
                         </xsl:choose>                                                 
                       </xsl:when>          
                       <xsl:otherwise>
                         <xsl:value-of select="'00:00.000Z'"/>
                       </xsl:otherwise>
                     </xsl:choose>                                              
                   </xsl:when>          
                   <xsl:otherwise>
                     <xsl:value-of select="'00:00:00.000Z'"/>
                   </xsl:otherwise>
                 </xsl:choose>                                 
               </xsl:when>          
               <xsl:otherwise>
                 <xsl:value-of select="'01T00:00:00.000Z'"/>
               </xsl:otherwise>
             </xsl:choose>
           </xsl:when>          
           <xsl:otherwise>
             <xsl:value-of select="'01-01T00:00:00.000Z'"/>
           </xsl:otherwise>
         </xsl:choose>              
      </xsl:when>         
    </xsl:choose>   
  </xsl:template>
</xsl:stylesheet>
