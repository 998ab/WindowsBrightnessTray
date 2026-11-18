param([string]$PreviewPath)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$root=Split-Path $PSScriptRoot -Parent
$sizes=@(16,20,24,32,40,48,64,128,256)
$frames=[System.Collections.Generic.List[byte[]]]::new()
& {
    foreach($size in $sizes) {
        $bitmap=[System.Drawing.Bitmap]::new($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics=[System.Drawing.Graphics]::FromImage($bitmap)
        $stream=[System.IO.MemoryStream]::new()
        try {
            # Use the current native sun, rather than the historical raster logo.
            $graphics.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.ScaleTransform($size/32.0,$size/32.0)
            $graphics.TranslateTransform(16,16)
            $outline=[System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(235,255,255,255),3.2)
            $ink=[System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(26,26,26),2.0)
            try {
                foreach($pen in @($outline,$ink)) {
                    $pen.StartCap=[System.Drawing.Drawing2D.LineCap]::Round
                    $pen.EndCap=[System.Drawing.Drawing2D.LineCap]::Round
                    $graphics.DrawEllipse($pen,[single]-6.7,[single]-6.7,[single]13.4,[single]13.4)
                    for($ray=0;$ray -lt 8;$ray++) {
                        $graphics.DrawLine($pen,[single]0,[single]-10.8,[single]0,[single]-13.7)
                        $graphics.RotateTransform(45)
                    }
                }
            } finally {$outline.Dispose();$ink.Dispose()}
            if($PreviewPath -and $size -eq 256){$bitmap.Save([IO.Path]::GetFullPath($PreviewPath),[System.Drawing.Imaging.ImageFormat]::Png)}
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
}
