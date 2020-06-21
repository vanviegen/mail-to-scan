## Admin Scan

A nasty little script that takes photos from email attachments, converts them to properly 'scanned' documents, and forwards them to another email address. It will attempt to:
- crop,
- deskew,
- scale to A4,
- lighten (to make white backgrounds fully white),
- rotate (based on text recognition),
- convert (multiple images) to a (multi-page) PDF,
- make the PDF searchable (using OCR), and
- leave alone non-image attachments.

It uses:
- ripmime for extracting attachments from email,
- a custom C++ program for cropping, deskewing and ligtening,
- Tesseract for OCR,
- exiftool for scaling,
- ImageMagick for converting other image formats to JPEG,
- jpegtran for rotation,
- pdftk for merging PDFs, and
- sendmail for forwarding the results.

Besides that, you'll need...
- PHP to run the script, and
- exim to receive email and forward it to the script (some other mail server would probably work as well).


## Caution

As mentioned, this is a *nasty* little script. It barely does any error handling, and will probable not handle weird input all that well. Code quality is low - it's the result of a day of furious hacking, and has not been productized. **Security problems are quite likely.**  In other words: for serious use, this software requires some more work. I'm throwing it out there in the hope that somebody 

The C++ part (for autocropping, deskewing and lightening) is relatively simple, but somehow seems to work rather reliable.


## Installation

```sh
sudo apt-get install ripmime tesseract-ocr tesseract-ocr-nld tesseract-ocr-eng exiftool pdftk libopencv-imgproc2.4v5 libopencv-core2.4v5 libopencv-highgui2.4-deb0
git clone https://github.com/vanviegen/mail-to-scan
```

Within `/etc/aliases` create an alias such as:

```
invoices: "|/usr/bin/php /your/path/mail-to-scan/mail-to-scan.php target-address@example.com"
```

The target address may be that of your bookkeeper or bookkeeping SaaS.

If you're using a different architecture than x86-64, or different library versions, you'll need to compile `deskew` yourself:

```sh
sudo apt-get install libopencv-dev g++
g++ -O2 -std=c++11 -lopencv_imgproc -lopencv_highgui -lopencv_photo -lopencv_core -o deskew deskew.cpp
```


## Usage

Send emails containing photo attachments to the email address you're relaying to the script. Presumable one would do this from a smart phone. Attaching multiple photos to a single email will create a single multi-page PDF, using the order in which the attachments are added.

For proper cropping and deskewing:
- Put your original on a contrasting background.
- Make sure the lighting is reasonably uniform.
- Have the document fill about 90% of the photo frame. There *should* be some background visible on all four sides.

After processing, a copy of the resulting PDF is mailed to the original sender. Don't destroy your original document until you've confirmed all is well.


## License

MIT.

