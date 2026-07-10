using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace osuReplayEditor
{
    public class HitErrorBarControl : Control
    {
        private struct HitErrorMarker
        {
            public int TimeMs;
            public int ErrorMs;
            public int Points;
        }

        private readonly List<HitErrorMarker> markers = new List<HitErrorMarker>();

        private int currentTimeMs;
        private float greatWindowMs = 20.0f;
        private float okWindowMs = 60.0f;
        private float mehWindowMs = 100.0f;
        private int lookbackMs = 4500;

        private readonly Brush backgroundBrush = new SolidBrush(Color.FromArgb(14, 17, 20));
        private readonly Pen trackPen = new Pen(Color.FromArgb(80, 240, 245, 250), 2.0f);
        private readonly Pen mehBoundaryPen = new Pen(Color.FromArgb(90, 255, 220, 65), 1.0f);
        private readonly Pen okBoundaryPen = new Pen(Color.FromArgb(130, 255, 70, 245), 1.0f);
        private readonly Pen greatBoundaryPen = new Pen(Color.FromArgb(160, 118, 255, 255), 1.5f);
        private readonly Pen centrePen = new Pen(Color.FromArgb(245, 245, 245, 245), 2.0f);

        public HitErrorBarControl()
        {
            DoubleBuffered = true;
            SetStyle(
                ControlStyles.UserPaint |
                ControlStyles.AllPaintingInWmPaint |
                ControlStyles.OptimizedDoubleBuffer |
                ControlStyles.ResizeRedraw,
                true);
        }

        public int CurrentTimeMs
        {
            get { return currentTimeMs; }
            set
            {
                currentTimeMs = value;
                Invalidate();
            }
        }

        public void SetWindows(float great, float ok, float meh)
        {
            greatWindowMs = Math.Max(1.0f, great);
            okWindowMs = Math.Max(greatWindowMs + 1.0f, ok);
            mehWindowMs = Math.Max(okWindowMs + 1.0f, meh);
            Invalidate();
        }

        public void SetMarkers(int[] times, int[] errors, int[] points, int count)
        {
            markers.Clear();
            if (times != null && errors != null && points != null)
            {
                int safeCount = Math.Min(count, Math.Min(times.Length, Math.Min(errors.Length, points.Length)));
                for (int i = 0; i < safeCount; ++i)
                {
                    markers.Add(new HitErrorMarker
                    {
                        TimeMs = times[i],
                        ErrorMs = errors[i],
                        Points = points[i]
                    });
                }
            }
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent?.BackColor ?? Color.Black);

            Rectangle bounds = new Rectangle(0, 0, Math.Max(1, Width - 1), Math.Max(1, Height - 1));
            FillRoundedRectangle(g, backgroundBrush, bounds, 5);

            Rectangle track = new Rectangle(8, 0, Math.Max(1, Width - 16), Math.Max(1, Height));
            DrawMeter(g, track);
            DrawMarkers(g, track);
        }

        private void DrawMeter(Graphics g, Rectangle track)
        {
            int centerY = track.Top + track.Height / 2;
            DrawWindowBand(g, track, mehWindowMs, Color.FromArgb(220, 220, 172, 70), 16.0f);
            DrawWindowBand(g, track, okWindowMs, Color.FromArgb(225, 87, 226, 19), 12.0f);
            DrawWindowBand(g, track, greatWindowMs, Color.FromArgb(235, 57, 198, 242), 8.0f);

            g.DrawLine(trackPen, track.Left, centerY, track.Right, centerY);
            DrawBoundary(g, track, 0, centrePen, 8);
            DrawBoundary(g, track, -greatWindowMs, greatBoundaryPen, 6);
            DrawBoundary(g, track, greatWindowMs, greatBoundaryPen, 6);
            DrawBoundary(g, track, -okWindowMs, okBoundaryPen, 5);
            DrawBoundary(g, track, okWindowMs, okBoundaryPen, 5);
            DrawBoundary(g, track, -mehWindowMs, mehBoundaryPen, 4);
            DrawBoundary(g, track, mehWindowMs, mehBoundaryPen, 4);
        }

        private void DrawWindowBand(Graphics g, Rectangle track, float window, Color color, float width)
        {
            int centerY = track.Top + track.Height / 2;
            using (Pen pen = new Pen(color, width))
            {
                pen.StartCap = LineCap.Square;
                pen.EndCap = LineCap.Square;
                g.DrawLine(pen, ErrorToX(-window, track), centerY, ErrorToX(window, track), centerY);
            }
        }

        private void DrawMarkers(Graphics g, Rectangle track)
        {
            int minTime = currentTimeMs - lookbackMs;
            int maxTime = currentTimeMs + 250;
            int centerY = track.Top + track.Height / 2;

            foreach (HitErrorMarker marker in markers)
            {
                if (marker.TimeMs < minTime || marker.TimeMs > maxTime)
                    continue;

                int x = ErrorToX(marker.ErrorMs, track);
                Color baseColor = marker.Points >= 300
                    ? Color.FromArgb(118, 255, 255)
                    : marker.Points >= 100
                        ? Color.FromArgb(255, 70, 245)
                        : Color.FromArgb(255, 220, 65);

                int age = Math.Max(0, currentTimeMs - marker.TimeMs);
                int alpha = Math.Max(80, 255 - (int)(age / (double)Math.Max(1, lookbackMs) * 180.0));

                int markerHalfHeight = marker.Points >= 300 ? 7 : marker.Points >= 100 ? 8 : 9;
                using (Pen pen = new Pen(Color.FromArgb(alpha, baseColor), marker.Points >= 300 ? 2.0f : 2.5f))
                {
                    g.DrawLine(pen, x, centerY - markerHalfHeight, x, centerY + markerHalfHeight);
                }
            }
        }

        private void DrawBoundary(Graphics g, Rectangle track, float error, Pen pen, int halfHeight)
        {
            int x = ErrorToX(error, track);
            int centerY = track.Top + track.Height / 2;
            g.DrawLine(pen, x, centerY - halfHeight, x, centerY + halfHeight);
        }

        private int ErrorToX(float error, Rectangle track)
        {
            double half = track.Width / 2.0;
            double clamped = Math.Max(-mehWindowMs, Math.Min(mehWindowMs, error));
            return track.Left + (int)(half + clamped / mehWindowMs * half + 0.5);
        }

        private static void FillRoundedRectangle(Graphics g, Brush brush, Rectangle rect, int radius)
        {
            if (rect.Width <= 0 || rect.Height <= 0)
                return;

            int diameter = Math.Max(1, radius * 2);
            if (diameter > rect.Width || diameter > rect.Height)
            {
                g.FillRectangle(brush, rect);
                return;
            }

            using (GraphicsPath path = new GraphicsPath())
            {
                path.AddArc(rect.Left, rect.Top, diameter, diameter, 180, 90);
                path.AddArc(rect.Right - diameter, rect.Top, diameter, diameter, 270, 90);
                path.AddArc(rect.Right - diameter, rect.Bottom - diameter, diameter, diameter, 0, 90);
                path.AddArc(rect.Left, rect.Bottom - diameter, diameter, diameter, 90, 90);
                path.CloseFigure();
                g.FillPath(brush, path);
            }
        }
    }
}
