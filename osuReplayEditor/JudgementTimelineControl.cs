using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace osuReplayEditor
{
    public class JudgementTimelineControl : Control
    {
        private const int OverviewHeight = 70;
        private const int BackgroundInset = 35;
        private const int RailInset = 60;
        private const int RailY = 18;
        private const int MarkerTop = 10;
        private const int MarkerBottom = 30;
        private const int PlayheadDiameter = 9;

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

        private readonly Brush backgroundBrush = new SolidBrush(Color.FromArgb(23, 23, 23));
        private readonly Brush textBrush = new SolidBrush(Color.White);
        private readonly Brush playheadBrush = new SolidBrush(Color.White);
        private readonly Pen railPen = new Pen(Color.FromArgb(48, 48, 48), 3f);
        private readonly Pen missPen = new Pen(Color.FromArgb(231, 76, 60), 3f);
        private readonly Pen fiftyPen = new Pen(Color.FromArgb(241, 196, 15), 3f);
        private readonly Pen hundredPen = new Pen(Color.FromArgb(106, 176, 76), 3f);
        private readonly Font summaryFont = new Font("Segoe UI", 12.75f, FontStyle.Regular, GraphicsUnit.Point);
        private readonly StringFormat summaryFormat = new StringFormat
        {
            Alignment = StringAlignment.Center,
            LineAlignment = StringAlignment.Center
        };

        public JudgementTimelineControl() : base()
        {
            this.Value = 0;
            this.TimelineEndMs = 1;
            this.MinimumSize = new Size(RailInset * 2, OverviewHeight);
            this.DoubleBuffered = true;
            this.SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            this.Paint += Timeline_Paint;
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                backgroundBrush.Dispose();
                textBrush.Dispose();
                playheadBrush.Dispose();
                railPen.Dispose();
                missPen.Dispose();
                fiftyPen.Dispose();
                hundredPen.Dispose();
                summaryFont.Dispose();
                summaryFormat.Dispose();
            }

            base.Dispose(disposing);
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
            int left = RailLeft;
            int usableWidth = RailWidth;
            double ratio = Math.Max(0.0, Math.Min(1.0, (x - left) / (double)usableWidth));
            return (int)(ratio * timelineEndMs);
        }

        private void Timeline_Paint(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent?.BackColor ?? Color.FromArgb(246, 248, 252));

            Rectangle bounds = OverviewBounds;
            FillRoundedRectangle(g, backgroundBrush, bounds, 12);

            int railY = Math.Min(RailY, Math.Max(0, Height - 1));
            g.DrawLine(railPen, RailLeft, railY, RailRight, railY);
            DrawJudgementMarkers(g);
            DrawSummary(g);
            DrawPlayhead(g);
        }

        private void DrawJudgementMarkers(Graphics g)
        {
            int markerTop = Math.Min(MarkerTop, Math.Max(0, Height - 1));
            int markerBottom = Math.Min(MarkerBottom, Math.Max(markerTop, Height - 1));

            foreach (JudgementMarker marker in judgementMarkers)
            {
                int x = TimeToX(marker.TimeMs);
                g.DrawLine(GetMarkerPen(marker.Kind), x, markerTop, x, markerBottom);
            }
        }

        private void DrawSummary(Graphics g)
        {
            if (string.IsNullOrWhiteSpace(summaryText))
                return;

            float top = Math.Min(38.0f, Math.Max(0.0f, Height - 1.0f));
            RectangleF summaryRect = new RectangleF(0.0f, top, Width, Math.Max(1.0f, Height - top));
            g.DrawString(summaryText, summaryFont, textBrush, summaryRect, summaryFormat);
        }

        private void DrawPlayhead(Graphics g)
        {
            int railY = Math.Min(RailY, Math.Max(0, Height - 1));
            int x = RailLeft + (int)(Value * RailWidth + 0.5);
            int offset = PlayheadDiameter / 2;
            g.FillEllipse(playheadBrush, x - offset, railY - offset, PlayheadDiameter, PlayheadDiameter);
        }

        private int TimeToX(int timeMs)
        {
            double ratio = Math.Max(0.0, Math.Min(1.0, timeMs / (double)timelineEndMs));
            return RailLeft + (int)(ratio * RailWidth + 0.5);
        }

        private Pen GetMarkerPen(int kind)
        {
            if (kind == 0) return missPen;
            if (kind == 50) return fiftyPen;
            return hundredPen;
        }

        private Rectangle OverviewBounds
        {
            get
            {
                int inset = Width > BackgroundInset * 2 ? BackgroundInset : 0;
                return new Rectangle(inset, 0, Math.Max(1, Width - inset * 2), Math.Max(1, Math.Min(OverviewHeight, Height)));
            }
        }

        private int RailLeft
        {
            get { return Width > RailInset * 2 ? RailInset : 0; }
        }

        private int RailRight
        {
            get { return Width > RailInset * 2 ? Width - RailInset : Math.Max(0, Width - 1); }
        }

        private int RailWidth
        {
            get { return Math.Max(1, RailRight - RailLeft); }
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
