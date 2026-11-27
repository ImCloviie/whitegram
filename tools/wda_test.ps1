# Checks whether Windows lets an app hide its window from screen capture
# (what Whitegram streamer mode uses). Run: right click -> Run with PowerShell.
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @'
using System; using System.Drawing; using System.Runtime.InteropServices;
public static class Wda {
  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool SetWindowDisplayAffinity(IntPtr h, uint a);
  [DllImport("user32.dll")]
  public static extern IntPtr GetWindowLongPtr(IntPtr h, int i);
  [DllImport("user32.dll")]
  static extern IntPtr SetWindowLongPtr(IntPtr h, int i, IntPtr v);
  [StructLayout(LayoutKind.Sequential)] struct POINT { public int x, y; }
  [StructLayout(LayoutKind.Sequential)] struct SIZE { public int cx, cy; }
  [StructLayout(LayoutKind.Sequential, Pack=1)] struct BLEND { public byte op, flags, alpha, format; }
  [DllImport("user32.dll", SetLastError=true)]
  static extern bool UpdateLayeredWindow(IntPtr h, IntPtr dst, ref POINT pos, ref SIZE size, IntPtr src, ref POINT srcPos, int key, ref BLEND blend, int flags);
  [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
  [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
  [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
  [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);
  [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr o);

  // Makes the window layered the way Qt does for windows with alpha:
  // the content is given to Windows with UpdateLayeredWindow.
  public static bool MakePerPixelLayered(IntPtr h, Rectangle r) {
    var style = GetWindowLongPtr(h, -20).ToInt64();
    SetWindowLongPtr(h, -20, new IntPtr(style | 0x80000));
    using (var bmp = new Bitmap(r.Width, r.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb)) {
      using (var g = Graphics.FromImage(bmp)) g.Clear(Color.FromArgb(255, 30, 40, 60));
      var screen = GetDC(IntPtr.Zero);
      var mem = CreateCompatibleDC(screen);
      var hbmp = bmp.GetHbitmap(Color.FromArgb(0));
      var old = SelectObject(mem, hbmp);
      var pos = new POINT { x = r.X, y = r.Y };
      var size = new SIZE { cx = r.Width, cy = r.Height };
      var src = new POINT();
      var blend = new BLEND { op = 0, flags = 0, alpha = 255, format = 1 };
      var ok = UpdateLayeredWindow(h, screen, ref pos, ref size, mem, ref src, 0, ref blend, 2);
      SelectObject(mem, old); DeleteObject(hbmp); DeleteDC(mem); ReleaseDC(IntPtr.Zero, screen);
      return ok;
    }
  }
}
'@

function Test-Window($title, $mode, $big) {
	$form = New-Object System.Windows.Forms.Form
	$form.Text = $title
	if ($mode -eq 'layered') {
		# Opacity below 1 makes Windows Forms give the window WS_EX_LAYERED.
		$form.Opacity = 0.99
	}
	if ($big) {
		$form.StartPosition = 'Manual'
		$form.Bounds = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
	}
	$form.Show()
	$ulw = ''
	if ($mode -eq 'perpixel') {
		$ulw = ', update ' + [Wda]::MakePerPixelLayered($form.Handle, $form.Bounds)
	}
	$style = [Wda]::GetWindowLongPtr($form.Handle, -20).ToInt64()
	$ok = [Wda]::SetWindowDisplayAffinity($form.Handle, 0x11)
	$err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
	$form.Close()
	$name = "{0} (exstyle 0x{1:x}{2})" -f $title, $style, $ulw
	if ($ok) {
		Write-Host "OK   $name" -ForegroundColor Green
	} else {
		Write-Host "FAIL $name, error $err" -ForegroundColor Red
	}
}

Write-Host ("Windows " + [Environment]::OSVersion.Version)
Test-Window 'Plain window' 'plain' $false
Test-Window 'Layered window' 'layered' $false
Test-Window 'Big layered window' 'layered' $true
Test-Window 'Per-pixel layered window' 'perpixel' $false
Test-Window 'Big per-pixel layered window' 'perpixel' $true
Read-Host 'Press Enter to close'
