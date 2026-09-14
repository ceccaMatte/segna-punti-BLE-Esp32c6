<#
.SYNOPSIS
    Genera main/font_data.h con le tabelle di glifi anti-aliased.

.DESCRIPTION
    Rasterizza i caratteri richiesti con System.Drawing (GDI+, presente su
    Windows, nessuna installazione) ed emette un header C con bitmap alpha a
    8 bit in celle a dimensione fissa.

    Per ogni font si ottiene una cella larghezza x altezza identica per tutti i
    glifi, con la stessa origine in alto a sinistra: il disegno diventa una
    semplice copia e la centratura una moltiplicazione.

    Se un font ha Sample e TargetWidth, la dimensione del corpo viene cercata
    automaticamente: si prende il valore piu' grande per cui quella stringa
    entra davvero nei pixel disponibili. Cosi', cambiando il file TTF, non
    serve ritarare niente a mano.

.PARAMETER OutFile
    File da scrivere. Default: main/font_data.h

.PARAMETER FontDir
    Cartella dei font TrueType. Default: C:\Windows\Fonts

.PARAMETER ScoreStretch
    Allungamento verticale delle cifre del punteggio. 1.0 = proporzioni
    originali; valori sopra 1.0 rendono le cifre piu' alte e strette, utili per
    riempire il pannello. Il disegno resta allineato perche' agisce su tutti i
    glifi allo stesso modo.

.EXAMPLE
    .\tools\gen_font.ps1
    Rigenera main/font_data.h con i valori di default.

.EXAMPLE
    .\tools\gen_font.ps1 -ScoreStretch 1.2
    Cifre del punteggio piu' slanciate.

.NOTES
    I valori di default sono tarati sul pannello 172x320 della Waveshare
    ESP32-C6-LCD-1.47: due cifre devono entrare in 73 px utili.
    SPDX-License-Identifier: MIT
#>
[CmdletBinding()]
param(
    [string] $OutFile = '',
    [string] $FontDir = 'C:\Windows\Fonts',
    [double] $ScoreStretch = 1.0
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutFile) {
    $OutFile = Join-Path $ProjectRoot 'main\font_data.h'
}

Add-Type -AssemblyName System.Drawing

# ---------------------------------------------------------------------------
# Rasterizzatore (C#: i cicli sui pixel in PowerShell puro sarebbero lentissimi)
# ---------------------------------------------------------------------------

$rasterSource = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

public sealed class FontPlan
{
    public string Name;
    public string FontFile;
    public float  PixelSize;
    public bool   Bold;
    public string CharSet;
    public int    Spacing;
    public string Sample;
    public int    TargetWidth;
    public int    MaxHeight;
    public double Stretch = 1.0;

    public int CellHeight;
    public int NaturalHeight;
    public int ResolvedSize;
    public int[] Advances;
}

public sealed class FontMetrics
{
    public int  NaturalHeight;
    public int  CellHeight;
    public int  CommonTop;
    public bool Clipped;
}

public sealed class Raster
{
    public byte[] Pixels;
    public int Width;
    public int Height;
    public int Left  = -1;
    public int Right = -1;
    public int Top   = -1;
    public int Bottom = -1;
    public bool Clipped;
}

public static class FontGen
{
    private const int Margin = 6;

    // La PrivateFontCollection deve restare viva finche' si usano i Font che ne
    // derivano: se finisce in garbage collection GDI+ va in access violation
    // dentro DrawString. Le teniamo quindi in cache per tutta l'esecuzione.
    private static readonly Dictionary<string, PrivateFontCollection> FontCollections =
        new Dictionary<string, PrivateFontCollection>();

    private static readonly Dictionary<string, FontFamily> FontFamilies =
        new Dictionary<string, FontFamily>();

    private static FontFamily LoadFamily(string path)
    {
        FontFamily cached;

        if (FontFamilies.TryGetValue(path, out cached))
        {
            return cached;
        }

        var collection = new PrivateFontCollection();
        collection.AddFontFile(path);

        if (collection.Families.Length == 0)
        {
            throw new Exception("Font non caricabile: " + path);
        }

        FontCollections[path] = collection;
        FontFamilies[path] = collection.Families[0];

        return FontFamilies[path];
    }

    private static Font MakeFont(FontFamily family, float pixels, bool bold)
    {
        // GraphicsUnit.Pixel: la dimensione e' l'altezza del corpo in pixel,
        // indipendente dal DPI della macchina che rigenera il font.
        FontStyle style = bold ? FontStyle.Bold : FontStyle.Regular;
        return new Font(family, pixels, style, GraphicsUnit.Pixel);
    }

    private static Raster RenderChar(Font font, char c, int width, int height, int margin)
    {
        var raster = new Raster { Width = width, Height = height, Pixels = new byte[width * height] };

        using (var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb))
        {
            using (var g = Graphics.FromImage(bitmap))
            {
                g.Clear(Color.Black);
                g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                // il disegno parte dal margine: cosi' l'inchiostro non tocca mai
                // i bordi e nessun glifo viene troncato
                g.DrawString(c.ToString(), font, Brushes.White, (float)margin, (float)margin, StringFormat.GenericTypographic);
            }

            var rect = new Rectangle(0, 0, width, height);
            var data = bitmap.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                int stride = data.Stride;
                var buffer = new byte[stride * height];
                Marshal.Copy(data.Scan0, buffer, 0, buffer.Length);

                int left = width, right = -1, top = height, bottom = -1;

                for (int y = 0; y < height; y++)
                {
                    int row = y * stride;
                    for (int x = 0; x < width; x++)
                    {
                        // Formato BGRA: il canale R e' la copertura, perche' il
                        // testo e' bianco su fondo nero.
                        byte coverage = buffer[row + x * 4 + 2];
                        raster.Pixels[y * width + x] = coverage;

                        if (coverage != 0)
                        {
                            if (x < left)   left = x;
                            if (x > right)  right = x;
                            if (y < top)    top = y;
                            if (y > bottom) bottom = y;
                        }
                    }
                }

                raster.Left   = (right >= 0) ? left : -1;
                raster.Right  = right;
                raster.Top    = (bottom >= 0) ? top : -1;
                raster.Bottom = bottom;
                raster.Clipped = (left == 0) || (top == 0) || (right == width - 1) || (bottom == height - 1);
            }
            finally
            {
                bitmap.UnlockBits(data);
            }
        }

        return raster;
    }

    private static FontMetrics MeasureFont(FontPlan plan, int size, out Dictionary<char, Raster> cache)
    {
        var family = LoadFamily(plan.FontFile);
        cache = new Dictionary<char, Raster>();

        using (var font = MakeFont(family, size, plan.Bold))
        {
            int margin = MarginFor(font);
            int width, height;
            ComputeCanvas(font, margin, plan.CharSet, out width, out height);

            int top = height, bottom = -1;
            bool clipped = false;

            foreach (char c in plan.CharSet)
            {
                var raster = RenderChar(font, c, width, height, margin);
                cache[c] = raster;
                clipped |= raster.Clipped;

                if (raster.Bottom >= 0)
                {
                    if (raster.Top < top)       top = raster.Top;
                    if (raster.Bottom > bottom) bottom = raster.Bottom;
                }
            }

            var metrics = new FontMetrics { Clipped = clipped };

            if (bottom < 0)
            {
                metrics.NaturalHeight = 1;
                metrics.CellHeight = 1;
                metrics.CommonTop = 0;
                return metrics;
            }

            // banda verticale comune a tutti i glifi: e' cio' che tiene la
            // linea di base allineata senza gestire bearing per carattere
            int natural = bottom - top + 1;
            int stretched = (int)Math.Round(natural * plan.Stretch);
            if (stretched < 1) stretched = 1;

            metrics.NaturalHeight = natural;
            metrics.CellHeight = stretched;
            metrics.CommonTop = top;

            return metrics;
        }
    }

    private static int MarginFor(Font font)
    {
        // MeasureString sottostima la larghezza dell'inchiostro, soprattutto in
        // grassetto: un margine proporzionale al corpo evita di troncare i glifi.
        int margin = (int)Math.Ceiling((double)font.Height / 3.0);
        return (margin < 8) ? 8 : margin;
    }

    private static void ComputeCanvas(Font font, int margin, string charSet, out int width, out int height)
    {
        float widest = 0f;

        using (var probe = new Bitmap(1, 1))
        using (var g = Graphics.FromImage(probe))
        {
            foreach (char c in charSet)
            {
                var size = g.MeasureString(c.ToString(), font, PointF.Empty, StringFormat.GenericTypographic);
                if (size.Width > widest) widest = size.Width;
            }
        }

        width  = (int)Math.Ceiling((double)widest) + 2 * margin;
        height = (int)Math.Ceiling((double)font.Height) + 2 * margin;

        if (width < 2 * margin + 1)   width = 2 * margin + 1;
        if (height < 2 * margin + 1)  height = 2 * margin + 1;
    }

    private static bool Fits(FontPlan plan, int size)
    {
        Dictionary<char, Raster> cache;
        FontMetrics metrics = MeasureFont(plan, size, out cache);

        int total = 0;

        // la larghezza si misura sulla stringa campione, glifo per glifo:
        // e' esattamente cio' che fara' font_measure_text() sul firmware
        foreach (char c in plan.Sample)
        {
            Raster raster;
            if (cache.TryGetValue(c, out raster) && raster.Right >= 0)
            {
                total += raster.Right - raster.Left + 1;
            }
        }

        if (plan.Sample.Length > 1)
        {
            total += (plan.Sample.Length - 1) * plan.Spacing;
        }

        if (total > plan.TargetWidth) return false;
        if (plan.MaxHeight > 0 && metrics.CellHeight > plan.MaxHeight) return false;

        return true;
    }

    private static int AdvanceOf(Raster raster, int size)
    {
        if (raster != null && raster.Right >= 0)
        {
            return raster.Right - raster.Left + 1;
        }

        // glifi senza inchiostro, tipicamente lo spazio
        int fallback = (int)Math.Round(size * 0.33);
        return (fallback < 1) ? 1 : fallback;
    }

    public static void Resolve(FontPlan plan)
    {
        int size = (int)Math.Round((double)plan.PixelSize);

        if (!string.IsNullOrEmpty(plan.Sample) && plan.TargetWidth > 0)
        {
            int low = 6;
            int high = 260;

            while (low < high)
            {
                int middle = (low + high + 1) / 2;
                if (Fits(plan, middle)) low = middle; else high = middle - 1;
            }

            size = low;
        }

        Dictionary<char, Raster> cache;
        FontMetrics metrics = MeasureFont(plan, size, out cache);

        plan.ResolvedSize  = size;
        plan.CellHeight    = metrics.CellHeight;
        plan.NaturalHeight = metrics.NaturalHeight;

        plan.Advances = new int[plan.CharSet.Length];
        for (int i = 0; i < plan.CharSet.Length; i++)
        {
            plan.Advances[i] = AdvanceOf(cache[plan.CharSet[i]], size);
        }

        if (metrics.Clipped)
        {
            Console.WriteLine("  ATTENZIONE: possibile clipping in " + plan.Name);
        }
    }

    private static byte[] CropResize(FontPlan plan, Raster raster, int advance, int originY)
    {
        int natural = plan.NaturalHeight;
        int target  = plan.CellHeight;
        int originX = (raster.Right >= 0) ? raster.Left : 0;

        // ritaglio sull'inchiostro del singolo glifo: la larghezza e' sua, non
        // del font. Verticalmente invece si usa la banda comune a tutti, cosi'
        // la linea di base resta allineata.
        var source = new byte[advance * natural];

        if (raster.Right >= 0)
        {
            for (int y = 0; y < natural; y++)
            {
                int srcY = originY + y;
                if (srcY < 0 || srcY >= raster.Height) continue;

                for (int x = 0; x < advance; x++)
                {
                    int srcX = originX + x;
                    if (srcX < 0 || srcX >= raster.Width) continue;

                    source[y * advance + x] = raster.Pixels[srcY * raster.Width + srcX];
                }
            }
        }

        if (target == natural)
        {
            return source;
        }

        // stiramento solo verticale, interpolazione lineare
        var scaled = new byte[advance * target];

        for (int y = 0; y < target; y++)
        {
            double sy = (y + 0.5) * natural / target - 0.5;

            int y0 = (int)Math.Floor(sy);
            double fy = sy - y0;

            if (y0 < 0) { y0 = 0; fy = 0.0; }

            int y1 = y0 + 1;
            if (y1 >= natural) { y1 = natural - 1; fy = 0.0; }

            for (int x = 0; x < advance; x++)
            {
                byte a = source[y0 * advance + x];
                byte b = source[y1 * advance + x];
                scaled[y * advance + x] = (byte)(a + (b - a) * fy + 0.5);
            }
        }

        return scaled;
    }

    public static string Generate(FontPlan[] plans)
    {
        var output = new StringBuilder();

        output.AppendLine("/*");
        output.AppendLine(" * Tabelle di glifi anti-aliased - GENERATO da tools/gen_font.ps1.");
        output.AppendLine(" * Non modificare a mano: rilancia lo script.");
        output.AppendLine(" *");
        output.AppendLine(" * Ogni glifo e' una bitmap di copertura di advance x cell_height byte,");
        output.AppendLine(" * 0 = trasparente, 255 = pieno, con origine in alto a sinistra.");
        output.AppendLine(" * Le altezze sono comuni a tutto il font, cosi' la linea di base resta");
        output.AppendLine(" * allineata; le larghezze sono per glifo, quindi il font e' proporzionale.");
        output.AppendLine(" *");
        output.AppendLine(" * SPDX-License-Identifier: MIT");
        output.AppendLine(" */");
        output.AppendLine();
        output.AppendLine("#pragma once");
        output.AppendLine();
        output.AppendLine("#include \"font.h\"");
        output.AppendLine();

        foreach (var plan in plans)
        {
            output.AppendLine("/* " + plan.Name + " - " + System.IO.Path.GetFileName(plan.FontFile) +
                              " corpo " + plan.ResolvedSize + " px - altezza cella " + plan.CellHeight +
                              " px - spaziatura " + plan.Spacing + " */");

            Dictionary<char, Raster> cache;
            FontMetrics metrics = MeasureFont(plan, plan.ResolvedSize, out cache);
            int originY = metrics.CommonTop;

            for (int i = 0; i < plan.CharSet.Length; i++)
            {
                string entry = plan.Name + "_g" + i.ToString("D2");
                byte[] pixels = CropResize(plan, cache[plan.CharSet[i]], plan.Advances[i], originY);

                output.Append("static const uint8_t " + entry + "[" + pixels.Length + "] = {");

                for (int j = 0; j < pixels.Length; j++)
                {
                    if ((j % 16) == 0) output.Append("\n    ");
                    output.Append(pixels[j].ToString(CultureInfo.InvariantCulture));
                    if (j < pixels.Length - 1) output.Append(", ");
                }

                output.Append("\n};\n\n");
            }

            output.Append("static const glyph_t " + plan.Name + "_gl[" + plan.CharSet.Length + "] = {");

            for (int i = 0; i < plan.CharSet.Length; i++)
            {
                if ((i % 4) == 0) output.Append("\n    ");
                output.Append("{ " + plan.Advances[i] + ", " + plan.Name + "_g" + i.ToString("D2") + " }");
                if (i < plan.CharSet.Length - 1) output.Append(", ");
            }

            output.Append("\n};\n\n");

            output.AppendLine("static const font_t " + plan.Name + " = { " +
                              plan.CellHeight + ", " + plan.Spacing + ", \"" +
                              Escape(plan.CharSet) + "\", " + plan.CharSet.Length + ", " + plan.Name + "_gl };");
            output.AppendLine();
        }

        return output.ToString();
    }

    private static string Escape(string text)
    {
        var builder = new StringBuilder();
        foreach (char c in text)
        {
            if (c == '\\' || c == '"') builder.Append('\\');
            builder.Append(c);
        }
        return builder.ToString();
    }
}
'@

# PowerShell tiene i tipi compilati in memoria per tutta la sessione: se lo
# script viene rilanciato senza riaprire il terminale, il secondo Add-Type
# fallirebbe con "il nome del tipo esiste gia'".
if ('FontGen' -as [type]) {
    Write-Warning 'Tipo FontGen gia caricato in questa sessione PowerShell: uso quella versione. Riapri il terminale per ricompilare.'
} else {
    Add-Type -TypeDefinition $rasterSource -ReferencedAssemblies 'System.Drawing'
}

# ---------------------------------------------------------------------------
# Specifiche dei font
# ---------------------------------------------------------------------------

# Il pannello del punteggio e' largo 81 px: togliendo i margini restano 73 px
# utili, ed e' questo il vincolo di larghezza per le cifre grandi.
$scoreWidth  = 73
$scoreChars  = '0123456789AD'
$labelChars  = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -'
$tinyChars   = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -:.'

# Arial Narrow Bold e' il condensato piu' stretto disponibile di serie su
# Windows: serve perche' due cifre entrino nei 73 px mantenendo una buona
# altezza. Cambiando FontFile la dimensione si ricalcola da sola.
$scoreFont = 'ARIALNB.TTF'
$labelFont = 'ARIALNB.TTF'
$tinyFont  = 'arialbd.ttf'

[FontPlan[]] $plans = @(
    # I campioni sono i casi peggiori, non stringhe comode: dentro una font le
    # cifre non hanno tutte la stessa larghezza ('4' e' la piu' larga) e le
    # maiuscole sono piu' larghe delle cifre. Dimensionando sulla stringa piu'
    # larga possibile, ogni punteggio piu' stretto entra a maggior ragione.
    [FontPlan]@{
        Name = 'font_score';   FontFile = $scoreFont; Bold = $true
        CharSet = $scoreChars; Spacing = 2; Sample = '44';  TargetWidth = $scoreWidth
        MaxHeight = 200; Stretch = $ScoreStretch; PixelSize = 80
    },
    [FontPlan]@{
        # tre cifre larghe sono piu' larghe di "AD", quindi tarando su "444"
        # entrano allo stesso livello sia il vantaggio sia un tie-break a tre cifre
        Name = 'font_score_s'; FontFile = $scoreFont; Bold = $true
        CharSet = $scoreChars; Spacing = 2; Sample = '444'; TargetWidth = $scoreWidth
        MaxHeight = 200; Stretch = $ScoreStretch; PixelSize = 54
    },
    [FontPlan]@{
        Name = 'font_score_xs'; FontFile = $scoreFont; Bold = $true
        CharSet = $scoreChars; Spacing = 2; Sample = '4444'; TargetWidth = $scoreWidth
        MaxHeight = 200; Stretch = $ScoreStretch; PixelSize = 36
    },
    [FontPlan]@{
        Name = 'font_label';   FontFile = $labelFont; Bold = $true
        CharSet = $labelChars; Spacing = 3; Sample = ''; TargetWidth = 0
        MaxHeight = 0; Stretch = 1.0; PixelSize = 20
    },
    [FontPlan]@{
        Name = 'font_tiny';    FontFile = $tinyFont;  Bold = $true
        CharSet = $tinyChars;  Spacing = 2; Sample = ''; TargetWidth = 0
        MaxHeight = 0; Stretch = 1.0; PixelSize = 13
    }
)

# ---------------------------------------------------------------------------
# Generazione
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host 'Generazione tabelle font' -ForegroundColor Cyan
Write-Host "Cartella font : $FontDir"
Write-Host "Destinazione  : $OutFile"
Write-Host ''

foreach ($plan in $plans) {

    $path = Join-Path $FontDir $plan.FontFile

    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "Font non trovato: $path" -ForegroundColor Red
        Write-Host 'Usa -FontDir per indicare un''altra cartella.' -ForegroundColor Yellow
        exit 1
    }

    $plan.FontFile = $path

    [FontGen]::Resolve($plan)

    $maxAdvance = 0
    foreach ($advance in $plan.Advances) {
        if ($advance -gt $maxAdvance) { $maxAdvance = $advance }
    }

    Write-Host ("  {0,-14} {1,-16} corpo {2,3} px   altezza {3,3} px   glifo max {4,3} px   {5,2} caratteri" -f `
        $plan.Name, (Split-Path -Leaf $path), $plan.ResolvedSize, `
        $plan.CellHeight, $maxAdvance, $plan.CharSet.Length)
}

Write-Host ''

$header = [FontGen]::Generate($plans)

# Scrittura senza BOM e con fine riga LF: cosi' il generato e' identico su
# qualsiasi macchina e i diff restano puliti.
$encoding = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($OutFile, $header, $encoding)

$size = (Get-Item -LiteralPath $OutFile).Length
Write-Host ("Scritto {0} ({1:N0} byte)" -f $OutFile, $size) -ForegroundColor Green
Write-Host ''
