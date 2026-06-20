# xPDFSearch

xPDFSearch is a content plugin for Total Commander.  

## Content

1. [Plugin description](#1)
2. [Field descriptions](#2)
3. [System requirements](#3)
4. [Use](#4)
5. [Configuration](#5)
6. [Author contact](#6)
7. [License](#7)
8. [History](history.md)

## 1\. Plugin description<a id='1'></a>

xPDFSearch can be used to perform full text search in PDF files.  
In addition xPDFSearch provides meta data information from PDF files.  
It's possible to display title, subject, keywords, author, application, PDF producer, number of pages, PDF version, created and modified.  

Plugin can be used in Synchronize Directories to compare content of PDF files.  

## 2\. Field descriptions<a id='2'></a>

<table>
  <tbody>
    <tr>
      <td>Title</td>
      <td>The document title.</td>
    </tr>
    <tr>
      <td>Subject</td>
      <td>The document subject.</td>
    </tr>
    <tr>
      <td>Keywords</td>
      <td>Keywords describing the document.</td>
    </tr>
    <tr>
      <td>Author</td>
      <td>The document author.</td>
    </tr>
    <tr>
      <td>Application</td>
      <td>The application which has been used to create the document.</td>
    </tr>
    <tr>
      <td>PDF Producer</td>
      <td>The component which has been used to perform the conversion to PDF.</td>
    </tr>
    <tr>
      <td>Document Start</td>
      <td>The first approximately 1000 characters of the PDF document.</td>
    </tr>
    <tr>
      <td>First Row</td>
      <td>First row of the PDF document.</td>
    </tr>
    <tr>
      <td>Extensions</td>
      <td>List of declared PDF extensions, semicolon separated.</td>
    </tr>
    <tr>
      <td>Number Of Pages</td>
      <td>The number of pages of the document.</td>
    </tr>
    <tr>
      <td>Number Of Fontless Pages</td>
      <td>The number of pages without Font resource. It might indicate that page does not have text.</td>
    </tr>
    <tr>
      <td>Number Of Pages With Images</td>
      <td>The number of pages with Image XObjects. Does not detect inline images.</td>
    </tr>
    <tr>
      <td>PDF Version</td>
      <td>The PDF version of the document.</td>
    </tr>
    <tr>
      <td>Page Width</td>
      <td>The width of the first page.</td>
    </tr>
    <tr>
      <td>Page Height</td>
      <td>The height of the first page.</td>
    </tr>
    <tr>
      <td>Copying Allowed</td>
      <td>Indicates if copying text from the PDF document is allowed.</td>
    </tr>
    <tr>
      <td>Printing Allowed</td>
      <td>Indicates if it's allowed to print the PDF document.</td>
    </tr>
    <tr>
      <td>Adding Comments Allowed</td>
      <td>Indicates if adding comments to the PDF document is allowed.</td>
    </tr>
    <tr>
      <td>Changing Allowed</td>
      <td>Indicates if changing the PDF document is allowed.</td>
    </tr>
    <tr>
      <td>Encrypted</td>
      <td>Indicates if the PDF document is encrypted.</td>
    </tr>
    <tr>
      <td>Protected</td>
      <td>Indicates if the PDF document is protected and cannot be open without password.</td>
    </tr>
    <tr>
      <td>Tagged</td>
      <td>Indicates if the PDF document is tagged.</td>
    </tr>
    <tr>
      <td>Linearized</td>
      <td>Indicates if the first page of the PDF can be displayed without loading the whole
      file.</td>
    </tr>
    <tr>
      <td>Incremental</td>
      <td>Indicates if the PDF document has been modified by appending data.</td>
    </tr>
    <tr>
      <td>Signature field</td>
      <td>Indicates if the PDF document has Signature field set. This may indicate that the
      document is digitally signed.</td>
    </tr>
    <tr>
      <td>Outlined</td>
      <td>Indicates if the PDF document has Outlines (bookmarks).</td>
    </tr>
    <tr>
      <td>Embedded Files</td>
      <td>Indicates if the PDF document has embedded files in Catalog directory.
      It is not checked for files in pages annotations.</td>
    </tr>
    <tr>
      <td>Created</td>
      <td>The creation date of the document.</td>
    </tr>
    <tr>
      <td>Modified</td>
      <td>The date when the document has been modified.</td>
    </tr>
    <tr>
      <td>Metadata Date</td>
      <td>The XMP metadata date from http://ns.adobe.com/xap/1.0/ namespace.</td>
    </tr>
    <tr>
      <td>ID</td>
      <td>The PDF document ID</td>
    </tr>
    <tr>
      <td>PDF Attributes</td>
      <td>
        <table>
          <tbody>
            <thead>PDF indicators displayed as attributes</thead>
            <tr>
              <td>P</td>
              <td>Printing allowed</td>
            </tr>
            <tr>
              <td>C</td>
              <td>Copying allowed</td>
            </tr>
            <tr>
              <td>M</td>
              <td>Changing (<b>M</b>odifying) allowed</td>
            </tr>
            <tr>
              <td>N</td>
              <td>Adding Comments (<b>N</b>otes) Allowed</td>
            </tr>
            <tr>
              <td>I</td>
              <td>Incremental</td>
            </tr>
            <tr>
              <td>T</td>
              <td>Tagged</td>
            </tr>
            <tr>
              <td>L</td>
              <td>Linearized</td>
            </tr>
            <tr>
              <td>E</td>
              <td>Encrypted</td>
            </tr>
            <tr>
              <td>X</td>
              <td>Protected</td>
            </tr>
            <tr>
              <td>S</td>
              <td>Signature</td>
            </tr>
            <tr>
              <td>O</td>
              <td>Outlines/Bookmarks</td>
            </tr>
            <tr>
              <td>F</td>
              <td>Embedded Files</td>
            </tr>
          </tbody>
        </table>
      </td>
    </tr>
    <tr>
      <td>Conformance</td>
      <td>Indicates conformances of the document to the PDF/A, PDF/E PDF/X, PDF/UA or PDF/R standards.<br> Multiple conformances are semicolon separated, e.g. "PDF/A-1a;PDF/R-1.0".</td>
    </tr>
    <tr>
      <td>Encryption</td>
      <td>Indicates PDF encryption type.</td>
    </tr>
    <tr>
      <td>Created Raw</td>
      <td>The creation date of the document without conversion to FILETIME.</td>
    </tr>
    <tr>
      <td>Modified Raw</td>
      <td>The date when the document has been modified without conversion to FILETIME.</td>
    </tr>
    <tr>
      <td>Metadata Date Raw</td>
      <td>The XMP metadata date from http://ns.adobe.com/xap/1.0/ namespace without conversion to FILETIME.</td>
    </tr>
    <tr>
      <td>Outlines</td>
      <td>The Outlines (bookmarks) search is available in the search and compare functions of Total Commander.</td>
    </tr>
    <tr>
      <td>Text</td>
      <td>The fulltext search is available in the search and compare functions of Total Commander.</td>
    </tr>
  </tbody>
</table>

PDF 2.0 has deprecated usage of Document Info Directory. If PDF file does not have Document Info Directory, fields are read from PDF metadata:  

<table>
  <tbody>
  <tr><td>Title</td><td>dc:title</td></tr>
  <tr><td>Subject</td><td>dc:description</td></tr>
  <tr><td>Keywords</td><td>pdf:Keywords</td></tr>
  <tr><td>Author</td><td>dc:creator</td></tr>
  <tr><td>Producer</td><td>pdf:Producer</td></tr>
  <tr><td>Creator</td><td>xap:CreatorTool</td></tr>
  <tr><td>Created</td><td>xap:CreateDate</td></tr>
  <tr><td>Modified</td><td>xap:ModifyDate</td></tr>
  <tr><td>Metadata Date</td><td>xap:MetadataDate</td></tr>
  </tbody>
</table>

## 3\. System requirements<a id='3'></a>

Total Commander 6.50 or higher is required for this plugin.  
To use the Created, Modified and Metadata Date fields Total Commander 6.53 or higher is required.  

## 4\. Use<a id='4'></a>

### Start a full text search

1. In menu click Commands/Search.
2. Now activate the tab "Plugins".
3. Select Plugin in the Plugin combobox. The other comboboxes Property (=Text) and OP (=contains) are already set to appropriate values for full text search.
4. Enter the search text in the value field.
5. Press start search button.

Of course it's possible to search for the other fields as well.  

![](./images/text_search_eng.png)  

_Search for all PDF documents containing the word bicycle_  

The other fields can be additionally used in files by file type, custom columns, tooltips, and thumbnail view.  

### Compare two or more files

1. Open "Synchronize Directories"
2. Click on a small <kbd>>></kbd> button to activate "User-defined compare functions by file type"
3. Check "Use plugin compare functions"
4. Click <kbd>Add...</kbd> to add PDF file type
5. Specify `*.pdf` as file type and click OK
6. Select "xPDFSearch" plugin and one of its properties, e.g. "Compare Text"
7. Close dialogs with <kbd>OK</kbd> , <kbd>OK</kbd> , <kbd>OK</kbd>
8. Click <kbd>Compare</kbd>

![](./images/compare.jpg)  

_Define xPDFSearch as compare plugin_  

## 5\. Configuration<a id='5'></a>

xPDFSearch plugin can be configured in `xPDFSearch.ini` file:  

```
[xPDFSearch]
•  NoCache=0
   ◦  0=file caching disabled, fast reading of fields, unable to rename or alter attributes of the open PDF file
   ◦  1=disables file caching, slower reading of fields, allows renaming PDF file with values form xPDFSearch and changing PDF file attributes (not content of PDF file)
•  DiscardInvisibleText=1 discard all invisible characters
•  DiscardDiagonalText=1 discard all text that's not close to 0/90/180/270 degrees
•  DiscardClippedText=1 discard all clipped characters
•  MarginLeft=0 discard all characters left of mediaBox + marginLeft
•  MarginRight=0 discard all characters right of mediaBox - marginRight
•  MarginTop=0 discard all characters above of mediaBox - marginTop
•  MarginBottom=0 discard all characters bellow of mediaBox + marginBottom
•  PageContentsLengthMin=32 Minimal value of page stream length so page is not considered empty. Used Used for "Number of Fontless pages"  and "Number of pages with images" fields.
•  TextOutputMode=0 text formatting mode:
   ◦  0=reading order
   ◦  1=original physical layout
   ◦  2=simple one-column
   ◦  3=simple one-column2
   ◦  4=optimized for tables
   ◦  5=fixed-pitch/height layout
   ◦  6=keep text in content stream order
•  AppendExtensionLevel=0 append PDF Extension Level to PDF Version (PDF 1.7 Ext. Level 3 = 1.73)
•  RemoveDateRawDColon=0 remove D: from CreatedRaw and ModifiedRaw fields
•  AttrPrintingAllowed=P symbol for "Printing Allowed" attribute
•  AttrCopyingAllowed=C symbol for "Copying Allowed" attribute
•  AttrChangingAllowed=M symbol for "Changing Allowed" attribute
•  AttrAddingCommentsAllowed=N symbol for "Adding Comments Allowed" attribute
•  AttrIncremental=I symbol for "Incremental" attribute
•  AttrTagged=T symbol for "Tagged" attribute
•  AttrLinearized=L symbol for "Linearized" attribute
•  AttrEncrypted=E symbol for "Encrypted" attribute
•  AttrProtected=X symbol for "Protected" attribute
•  AttrSignatureField=S symbol for "Signature Field" attribute
•  AttrOutlined=O symbol for "Outlined" attribute
•  AttrEmbeddedFiles=F symbol for "Embedded Files" attribute
```

To omit specific PDF Attribute field, clear attribute symbol, e.g. `AttrEmbeddedFiles=`  

If there is no `xPDFSearch.ini` file located in plugin directory, plugin uses options from TC content ini file.  
Default location of TC content ini file is `%COMMANDER_PATH%\contplug.ini` .  
Location of the `[xPDFSearch]` section in TC content ini file can be changed in `wincmd.ini` file, e.g.:  

```
[ReplaceIniLocation]
•  xPDFSearch.wdx=%COMMANDER_PATH%\Plugins\WDX\xPDFSearch\relocated.ini
```

xPDFSearch plugin uses slightly modified [Xpdf library](http://www.xpdfreader.com/about.html). Xpdf can be configured via [xpdfrc file](http://www.xpdfreader.com/xpdfrc-man.html).  

## 6\. Author contact<a id='6'></a>

There is a [thread](https://www.ghisler.ch/board/viewtopic.php?t=78256) in the [Total Commander forum](http://www.ghisler.ch/) which can be used to discuss problems, bugs and suggestions.  

## 7\. License<a id='7'></a>

This Total Commander Plugin is licensed under the General Public License (GPL). The license can be found in the [LICENSE.md](../LICENSE.md).  
