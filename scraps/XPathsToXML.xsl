<?xml version="1.0" encoding="UTF-8"?>
<!-- Original xslt lifted from http://stackoverflow.com/questions/11385323/populate-xml-template-file-from-xpath-expressions/11387570#11387570 -->

<xsl:stylesheet version="2.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                xmlns:my="my:my">

  <xsl:output omit-xml-declaration="yes" indent="yes"/>

  <xsl:template match="/">
    <xsl:sequence select="my:subTree($vPop/@path/concat(.,'/',string(..)))"/>
  </xsl:template>

  <xsl:function name="my:subTree" as="node()*">
    <xsl:param name="pPaths" as="xs:string*"/>
    <xsl:for-each-group select="$pPaths"
                        group-adjacent="substring-before(substring-after(concat(., '/'), '/'), '/')">
      <xsl:if test="current-grouping-key()">
        <xsl:choose>
          <xsl:when test="substring-after(current-group()[1], current-grouping-key())">
            <xsl:element name="{substring-before(concat(current-grouping-key(), '['), '[')}">
              <xsl:sequence select="my:subTree(for $s in current-group()
                return
                concat('/',substring-after(substring($s, 2),'/'))
                )
                "/>
            </xsl:element>
          </xsl:when>
          <xsl:otherwise>
            <xsl:value-of select="current-grouping-key()"/>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:if>
    </xsl:for-each-group>
  </xsl:function>

  <xsl:variable name="vPop" as="element()*">
  </xsl:variable>

</xsl:stylesheet>