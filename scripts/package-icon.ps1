$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$root=Split-Path $PSScriptRoot -Parent
$image=[System.Drawing.Image]::FromFile((Join-Path $root 'assets/logo.png'))
$sizes=@(16,20,24,32,40,48,64,128,256)
$frames=[System.Collections.Generic.List[byte[]]]::new()
try {
    foreach($size in $sizes) {
        $bitmap=[System.Drawing.Bitmap]::new($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics=[System.Drawing.Graphics]::FromImage($bitmap)
        $stream=[System.IO.MemoryStream]::new()
        try {
            $graphics.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.CompositingMode=[System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.DrawImage($image,0,0,$size,$size)
            $bitmap.Save($stream,[System.Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        } finally { $graphics.Dispose();$bitmap.Dispose();$stream.Dispose() }
    }
    $output=[System.IO.File]::Create((Join-Path $root 'assets/app.ico'))
    $writer=[System.IO.BinaryWriter]::new($output)
    try {
        $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$sizes.Count)
        $offset=6+16*$sizes.Count
        for($i=0;$i -lt $sizes.Count;$i++) {
            $dimension=if($sizes[$i] -eq 256){0}else{$sizes[$i]}
            $writer.Write([byte]$dimension);$writer.Write([byte]$dimension)
            $writer.Write([byte]0);$writer.Write([byte]0);$writer.Write([uint16]1);$writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$i].Length);$writer.Write([uint32]$offset)
            $offset+=$frames[$i].Length
        }
        foreach($frame in $frames) { $writer.Write($frame) }
    } finally { $writer.Dispose();$output.Dispose() }
} finally { $image.Dispose() }
