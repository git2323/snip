Add-Type -AssemblyName System.Drawing

$output = Join-Path $PSScriptRoot "..\assets\snip.ico"
$output = [System.IO.Path]::GetFullPath($output)
$directory = Split-Path $output
New-Item -ItemType Directory -Force -Path $directory | Out-Null

$sizes = @(16, 32, 48, 256)
$pngs = @()
try {
    foreach ($size in $sizes) {
        $bitmap = New-Object Drawing.Bitmap($size, $size)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.Clear([Drawing.Color]::FromArgb(20, 29, 51))

        $scale = $size / 256.0
        $round = New-Object System.Drawing.Drawing2D.GraphicsPath
        $round.AddArc(12*$scale, 12*$scale, 48*$scale, 48*$scale, 180, 90)
        $round.AddArc(196*$scale, 12*$scale, 48*$scale, 48*$scale, 270, 90)
        $round.AddArc(196*$scale, 196*$scale, 48*$scale, 48*$scale, 0, 90)
        $round.AddArc(12*$scale, 196*$scale, 48*$scale, 48*$scale, 90, 90)
        $round.CloseFigure()
        $background = New-Object System.Drawing.Drawing2D.LinearGradientBrush((New-Object System.Drawing.RectangleF(0,0,$size,$size)), [Drawing.Color]::FromArgb(31, 52, 91), [Drawing.Color]::FromArgb(9, 133, 154), 45)
        $graphics.FillPath($background, $round)

        $paper = New-Object System.Drawing.Drawing2D.GraphicsPath
        $paper.AddPolygon(@([Drawing.PointF]::new(65*$scale, 42*$scale), [Drawing.PointF]::new(165*$scale, 42*$scale), [Drawing.PointF]::new(198*$scale, 75*$scale), [Drawing.PointF]::new(198*$scale, 212*$scale), [Drawing.PointF]::new(65*$scale, 212*$scale)))
        $graphics.FillPath((New-Object Drawing.SolidBrush -ArgumentList ([Drawing.Color]::WhiteSmoke)), $paper)
        $graphics.FillPolygon((New-Object Drawing.SolidBrush -ArgumentList ([Drawing.Color]::FromArgb(176, 225, 235))), @([Drawing.PointF]::new(165*$scale,42*$scale), [Drawing.PointF]::new(165*$scale,75*$scale), [Drawing.PointF]::new(198*$scale,75*$scale)))

        $pen1 = New-Object Drawing.Pen -ArgumentList ([Drawing.Color]::FromArgb(12, 155, 178)), (11*$scale); $pen1.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $pen1.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen2 = New-Object Drawing.Pen -ArgumentList ([Drawing.Color]::FromArgb(245, 177, 63)), (11*$scale); $pen2.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $pen2.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen3 = New-Object Drawing.Pen -ArgumentList ([Drawing.Color]::FromArgb(57, 105, 190)), (11*$scale); $pen3.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $pen3.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $graphics.DrawLine($pen1, 88*$scale, 105*$scale, 170*$scale, 105*$scale)
        $graphics.DrawLine($pen2, 88*$scale, 133*$scale, 153*$scale, 133*$scale)
        $graphics.DrawLine($pen3, 88*$scale, 161*$scale, 170*$scale, 161*$scale)

        $png = Join-Path $directory ("snip-{0}.png" -f $size)
        $bitmap.Save($png, [Drawing.Imaging.ImageFormat]::Png)
        $pngs += $png
        $graphics.Dispose(); $background.Dispose(); $round.Dispose(); $paper.Dispose(); $pen1.Dispose(); $pen2.Dispose(); $pen3.Dispose(); $bitmap.Dispose()
    }

    $stream = [IO.File]::Create($output)
    $writer = New-Object IO.BinaryWriter($stream)
    $writer.Write([UInt16]0); $writer.Write([UInt16]1); $writer.Write([UInt16]$sizes.Count)
    $offset = 6 + (16 * $sizes.Count)
    $data = @()
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $bytes = [IO.File]::ReadAllBytes($pngs[$i]); $data += ,$bytes
        $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
        $writer.Write([Byte]$dimension); $writer.Write([Byte]$dimension); $writer.Write([Byte]0); $writer.Write([Byte]0)
        $writer.Write([UInt16]1); $writer.Write([UInt16]32); $writer.Write([UInt32]$bytes.Length); $writer.Write([UInt32]$offset)
        $offset += $bytes.Length
    }
    foreach ($bytes in $data) { $writer.Write($bytes) }
    $writer.Close(); $stream.Close()
}
finally {
    foreach ($png in $pngs) { Remove-Item -LiteralPath $png -Force -ErrorAction SilentlyContinue }
}
