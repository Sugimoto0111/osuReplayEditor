using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace osuReplayEditor
{
    public class JudgementTimelineControl : Control
    {
        public struct JudgementMarker
        {
            public int TimeMs;
            public int Kind;
        }

        public double Value
        {
            get { return this.value; }
            set
            {
                double clamped = Math.Max(0.0, Math.Min(1.0, value));
                if (Math.Abs(clamped - this.value) > 0.000001)
                {
                    this.value = clamped;
                    this.Invalidate();
                }
            }
        }

        public int TimelineEndMs
        {
            get { return timelineEndMs; }
            set
            {
                timelineEndMs = Math.Max(1, value);
                Invalidate();
            }
        }

        public string SummaryText
        {
            get { return summaryText; }
            set
            {
                summaryText = value ?? string.Empty;
                Invalidate();
            }
        }

        private double value;
        private int timelineEndMs = 1;
        private string summaryText = string.Empty;
        private readonly List<JudgementMarker> judgementMarkers = new List<JudgementMarker>();

        private readonly Brush backgroundBrush = new SolidBrush(Color.FromArgb(16, 16, 18));
        private readonly Brush missBrush = new SolidBrush(Color.FromArgb(245, 74, 74));
        private readonly Brush fiftyBrush = new SolidBrush(Color.FromArgb(250, 213, 82));
        private readonly Brush hundredBrush = new SolidBrush(Color.FromArgb(95, 190, 92));
        private readonly Brush textBrush = new SolidBrush(Color.FromArgb(225, 228, 235));
        private readonly Brush playheadBrush = new SolidBrush(Color.WhiteSmoke);
        private readonly Pen railPen = new Pen(Color.FromArgb(55, 57, 61), 3f);

        public JudgementTimelineControl() : base()
        {
            this.Value = 0;
            this.TimelineEndMs = 1;
            this.DoubleBuffered = true;
            this.SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            this.Paint += Timeline_Paint;
        }

        public void SetJudgementMarkers(int[] times, int[] kinds, int count)
        {
            judgementMarkers.Clear();
            if (times == null || kinds == null)
            {
                Invalidate();
                return;
            }

            int safeCount = Math.Min(count, Math.Min(times.Length, kinds.Length));
            for (int i = 0; i < safeCount; ++i)
            {
                judgementMarkers.Add(new JudgementMarker { TimeMs = times[i], Kind = kinds[i] });
            }
            Invalidate();
        }

        public int XToTimeMs(int x)
        {
            const int pad = 8;
            int usableWidth = Math.Max(1, Width - pad * 2);
            double ratio = Math.Max(0.0, Math.Min(1.0, (x - pad) / (double)usableWidth));
            return (int)(ratio * timelineEndMs);
        }

        private void Timeline_Paint(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent?.BackColor ?? Color.FromArgb(246, 248, 252));

            Rectangle bounds = new Rectangle(0, 0, Math.Max(1, Width - 1), Math.Max(1, Height - 1));
            FillRoundedRectangle(g, backgroundBrush, bounds, 8);

            int pad = 8;
            int usableWidth = Math.Max(1, Width - pad * 2);
            int railY = Math.Max(14, Height / 3);
            int markerY = Math.Max(5, railY - 10);
            int markerHeight = Math.Max(16, Height - markerY - 12);

            g.DrawLine(railPen, pad, railY, Width - pad, railY);
            DrawJudgementMarkers(g, pad, usableWidth, markerY, markerHeight);
            DrawSummary(g);
            DrawPlayhead(g, pad, usableWidth);
        }

        private void DrawJudgementMarkers(Graphics g, int pad, int usableWidth, int markerY, int markerHeight)
        {
            foreach (JudgementMarker marker in judgementMarkers)
            {
                int x = TimeToX(marker.TimeMs, pad, usableWidth);
                Brush brush = marker.Kind == 0 ? missBrush : marker.Kind == 50 ? fiftyBrush : hundredBrush;
                FillRoundedRectangle(g, brush, new Rectangle(x - 2, markerY, 4, markerHeight), 2);
            }
        }

        private void DrawSummary(Graphics g)
        {
            if (string.IsNullOrWhiteSpace(summaryText))
                return;

            using (Font summaryFont = new Font(Font.FontFamily, Math.Max(8.5f, Font.SizeInPoints), FontStyle.Bold))
            {
                SizeF size = g.MeasureString(summaryText, summaryFont);
                float x = (Width - size.Width) / 2.0f;
                float y = Math.Max(Height - size.Height - 6, Height / 2.0f);
                g.DrawString(summaryText, summaryFont, textBrush, x, y);
            }
        }

        private void DrawPlayhead(Graphics g, int pad, int usableWidth)
        {
            int x = pad + (int)(Value * usableWidth + 0.5);
            using (Pen pen = new Pen(Color.WhiteSmoke, 2f))
            {
                g.DrawLine(pen, x, 5, x, Height - 3);
            }

            Point[] triangle =
            {
                new Point(x, 5),
                new Point(x - 5, 0),
                new Point(x + 5, 0),
            };
            g.FillPolygon(playheadBrush, triangle);
        }

        private int TimeToX(int timeMs, int pad, int usableWidth)
        {
            double ratio = Math.Max(0.0, Math.Min(1.0, timeMs / (double)timelineEndMs));
            return pad + (int)(ratio * usableWidth + 0.5);
        }

        private static void FillRoundedRectangle(Graphics g, Brush brush, Rectangle rect, int radius)
        {
            if (rect.Width <= 0 || rect.Height <= 0)
                return;

            int diameter = Math.Max(1, radius * 2);
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
