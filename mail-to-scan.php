#!/usr/bin/php7.0
<?php

set_error_handler(
    function(int $nSeverity, string $strMessage, string $strFilePath, int $nLineNumber){
        if(error_reporting()!==0) // Not error suppression operator @
            throw new \ErrorException($strMessage, /*nExceptionCode*/ 0, $nSeverity, $strFilePath, $nLineNumber);
    },
    /*E_ALL*/ -1
);

$targets = implode(",",array_slice($argv,1));
$programDir = dirname(__FILE__);

// Create a random tmp directory, called $tmp
$tmp = tempnam(sys_get_temp_dir(), 'mail-to-scan-');
unlink($tmp);
mkdir($tmp);

// Extract mail contents (for stdin) to the $tmp directory
system("ripmime -i - -d '$tmp' -e --verbose-contenttype -v > '$tmp/ripmime'");

// Parse mail headers
$headers = [];
foreach(explode("\n",file_get_contents("$tmp/_headers_")) as $line) {
	$line = trim($line);
	$line = explode(": ", $line, 2);
	if (!isset($line[1])) continue;
	$headers[strtolower($line[0])] = $line[1];
}

// Create a list of text parts and a list of attachment parts
$texts = [];
$attachments = [];
foreach(explode("\n", file_get_contents("$tmp/ripmime")) as $line) {
	$line = trim($line);
	if (!preg_match('/^Decoding content-type=([^ ]+) filename=(.*)$/', $line, $m)) {
		continue;
	}
	$ct = $m[1];
	$fn = $m[2];
	if ($ct === 'multipart/mixed') continue;
	if (substr($fn, 0, 8) === 'textfile') {
		$texts[] = [$fn,$ct,null];
	}
	else {
		$attachments[] = [$fn,$ct,$fn];
	}
}

// Convert all image attachments to PDF(s).
$forwardAttachments = []; // Created PDFs *and* other attachments in original email. [file name, content type, original name]
$replyAttachments = []; // Created PDFs only.
$group = [];
foreach($attachments as $attachment) {
	list($fn, $ct, $attachName) = $attachment;
	if (substr($ct,0,6) !== 'image/') {
		$forwardAttachments[] = $attachment;
		continue;
	}
	system("$programDir/deskew \"$tmp/$fn\" $tmp/_x.jpg > /dev/null 2> /dev/null", $ret);
	if ($ret==2) {
		// A mostly black image; can be used to separate multiple documents.
		endGroup();
		continue;
	}
	else if ($ret) {
		assert($ret<127);
		system("convert \"$tmp/$fn\" $tmp/_x.jpg");
	}
	$deg = intval(trim(`tesseract -psm 0 $tmp/_x.jpg stdout 2>&1 | grep 'Rotate:' | cut -d : -f 2`));
	if ($deg) {
		system("jpegtran -rotate $deg $tmp/_x.jpg > $tmp/_y.jpg");
		rename("$tmp/_y.jpg", "$tmp/_x.jpg");
	}
	list($iw,$ih) = getimagesize("$tmp/_x.jpg");
	$tw = $iw < $ih ? 21.0 : 29.7;
	$th = $iw < $ih ? 29.7 : 21.0;
	$res = max($iw/$tw, $ih/$th);
	system("exiftool -overwrite_original -ResolutionUnit=cm -XResolution=$res -YResolution=$res -q $tmp/_x.jpg");

	system("tesseract -psm 3 -l eng+nld $tmp/_x.jpg $tmp/_y pdf > /dev/null 2> /dev/null");
	rename("$tmp/_y.pdf", "$tmp/$fn.pdf");
	$group[] = "$tmp/$fn.pdf";
}
if (!empty($group)) endGroup();


$sender = ucfirst(str_replace('"', '', preg_replace('/(.)[ <@].*/', '\1', trim($headers['from']))));

$texts = count($texts) == 1 ? $texts[0] : [$texts, 'multipart/alternative', false];
$parts = array_merge([$texts], $forwardAttachments);

$ref = isset($headers['message-id']) ? "References: {$headers['message-id']}\r\n" : "";
$subject = $headers['subject'] ?? '(no subject)';
$subject .= ' ['.date("Y-m-d").']';

$marker = getMarker();
$fd = popen('/usr/sbin/sendmail -t', 'w');
//$fd = fopen('/tmp/admin.output', 'w');
//$fd = popen('cat > /tmp/lala', 'w');
write($fd, "From: {$headers['from']}\r\nTo: $targets\r\n{$ref}Subject: $subject\r\nContent-Type: multipart/mixed; boundary=$marker\r\n\r\n");
writeMultipart($fd, $marker, $parts);
pclose($fd);

if (count($replyAttachments)) {
	file_put_contents("$tmp/_converted.txt", <<<EOD
Hi $sender,

Your receipt has been converted to PDF and forwarded to $targets. Please check if the PDF looks okay before destroying the original documents!

Hints for proper cropping and deskewing:
- Put your original on a contrasting background.
- Make sure the lighting is reasonably uniform.
- Have the document fill about 90% of the photo frame. There *should* be some background visible on all four sides.

Best,
Mail-to-scan.
EOD
);
	$parts = array_merge([["_converted.txt", "text/plain", null]], $replyAttachments);
	$fd = popen('/usr/sbin/sendmail -t', 'w');
	write($fd, "From: Mail-to-scan\r\nTo: {$headers['from']}\r\n{$ref}Subject: converted: $subject\r\nContent-Type: multipart/mixed; boundary=$marker\r\n\r\n");
	writeMultipart($fd, $marker, $parts);
	pclose($fd);
}

system("rm -rf '$tmp'");


function getMarker()
{
	return "mail-to-scan-" . bin2hex(random_bytes(10));
}

function writeMultipart($fd, $marker, $items)
{
	global $tmp;
	foreach($items as $item) {
		list($fn,$ct,$attachName) = $item;
		write($fd, "\r\n--$marker\r\n");
		if (is_array($fn)) {
			$subMarker = getMarker();
			write($fd, "Content-Type: $ct; boundary=$subMarker\r\n\r\n");
			writeMultipart($fd, $subMarker, $fn);
		}
		else {
			write($fd, "Content-Type: $ct\r\nContent-Transfer-Encoding: base64\r\n");
			if ($attachName) write($fd, "Content-Disposition: attachment; filename=\"$attachName\"\r\n");
			write($fd, "\r\n");
			$fd2 = fopen($tmp.'/'.$fn, 'rb');
			while(!feof($fd2)) {
				$b = base64_encode(fread($fd2, 78000));
				$b = preg_replace('/.{78}/', "\\0\r\n", $b);
				write($fd, $b);
			}
			fclose($fd2);
		}
	}
	write($fd, "\r\n--$marker--");
}

function write($fd, $data)
{
	while(!empty($data)) {
		$res = fwrite($fd, $data);
		assert($res > 0);
		$data = substr($data, $res);
	}
}

function endGroup()
{
	global $group, $forwardAttachments, $replyAttachments, $headers, $tmp, $sender;
	static $num = 0;
	$num++;
	$name = date("Y-m-d_H:i:s") . '_' . $sender . '_' . $num . '.pdf';
	system("pdftk \"".implode('" "',$group)."\" cat output '$tmp/_out$num.pdf'");
	$forwardAttachments[] = ["_out$num.pdf", "application/pdf", $name];
	$replyAttachments[] = ["_out$num.pdf", "application/pdf", $name];
	$group = [];
}


