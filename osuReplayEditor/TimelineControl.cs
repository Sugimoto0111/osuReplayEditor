using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace osuReplayEditor
{
    public class TimelineControl : Control
    {
        private const int Key1Mask = 5;
        private const int Key2Mask = 10;
        private const int TimelinePad = 10;

        private struct ReplayFrameView
        {
            public int TimeMs;
            public int Keys;
        }

        private struct HitObjectView
        {
            public int StartMs;
            public int EndMs;
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
            get { return this.timelineEndMs; }
            set
            {
                this.timelineEndMs = Math.Max(1, value);
                this.Invalidate();
            }
        }

        public int ViewDurationMs
        {
            get { return this.viewDurationMs; }
            set
            {
                this.viewDurationMs = Math.Max(1, value);
                this.Invalidate();
            }
        }

        private double value;
        private int timelineEndMs = 1;
        private int viewStartMs = 0;
        private int viewEndMs = 2000;
        private int viewDurationMs = 2000;
        private readonly List<ReplayFrameView> frames = new List<ReplayFrameView>();
        private readonly List<HitObjectView> hitObjects = new List<HitObjectView>();

        private readonly Brush backgroundBrush = new SolidBrush(Color.FromArgb(17, 17, 19));
        private readonly Brush seekRailBrush = new SolidBrush(Color.FromArgb(48, 49, 52));
        private readonly Brush seekFillBrush = new SolidBrush(Color.FromArgb(212, 217, 225));
        private readonly Brush laneBrush = new SolidBrush(Color.FromArgb(35, 35, 36));
        private readonly Brush key1Brush = new SolidBrush(Color.FromArgb(187, 100, 218));
        private readonly Brush key2Brush = new SolidBrush(Color.FromArgb(249, 152, 65));
        private readonly Brush noteCircleBrush = new SolidBrush(Color.FromArgb(224, 230, 238));
        private readonly Brush noteSliderBrush = new SolidBrush(Color.FromArgb(170, 177, 188));
        private readonly Brush noteSpinnerBrush = new SolidBrush(Color.FromArgb(130, 138, 150));
        private readonly Brush textBrush = new SolidBrush(Color.FromArgb(190, 195, 204));
        private readonly Brush playheadBrush = new SolidBrush(Color.WhiteSmoke);
        private readonly Pen dividerPen = new Pen(Color.FromArgb(232, 235, 240), 2f);
        private readonly Pen noteOutlinePen = new Pen(Color.FromArgb(242, 245, 250), 1f);

        public TimelineControl() : base()
        {
            this.Value = 0;
            this.TimelineEndMs = 1;
            this.ViewDurationMs = 2000;
            this.TabStop = true;
            this.DoubleBuffered = true;
            this.SetStyle(
                ControlStyles.UserPaint |
                ControlStyles.AllPaintingInWmPaint |
                ControlStyles.OptimizedDoubleBuffer |
                ControlStyles.ResizeRedraw |
                ControlStyles.Selectable,
                true);
            this.Paint += Timeline_Paint;
        }

        public void SetReplayFrames(int[] times, int[] keys, int count)
        {
            this.frames.Clear();
            if (times != null && keys != null)
            {
                int safeCount = Math.Min(count, Math.Min(times.Length, keys.Length));
                for (int i = 0; i < safeCount; ++i)
                {
                    this.frames.Add(new ReplayFrameView { TimeMs = times[i], Keys = keys[i] });
                }
            }
            this.Invalidate();
        }

        public void SetHitObjects(int[] startTimes, int[] endTimes, int[] kinds, int count)
        {
            this.hitObjects.Clear();
            if (startTimes != null && endTimes != null && kinds != null)
            {
                int safeCount = Math.Min(count, Math.Min(startTimes.Length, Math.Min(endTimes.Length, kinds.Length)));
                for (int i = 0; i < safeCount; ++i)
                {
                    this.hitObjects.Add(new HitObjectView
                    {
                        StartMs = startTimes[i],
                        EndMs = endTimes[i],
                        Kind = kinds[i]
                    });
                }
            }
            this.Invalidate();
        }

        public void SetViewport(int centerMs, int minMs, int maxMs)
        {
            int rangeMin = Math.Min(minMs, maxMs);
            int rangeMax = Math.Max(minMs + 1, maxMs);
            int duration = Math.Max(1, viewDurationMs);

            int start = centerMs - duration / 2;
            int end = start + duration;

            if (rangeMax - rangeMin <= duration)
            {
                start = rangeMin;
                end = rangeMax;
            }
            else
            {
                if (start < rangeMin)
                {
                    start = rangeMin;
                    end = start + duration;
                }
                if (end > rangeMax)
                {
                    end = rangeMax;
                    start = end - duration;
                }
            }

            if (end <= start) end = start + 1;

            viewStartMs = start;
            viewEndMs = end;
            Value = (centerMs - viewStartMs) / (double)Math.Max(1, viewEndMs - viewStartMs);
            Invalidate();
        }

        public int XToTimeMs(int x)
        {
            double ratio = Math.Max(0.0, Math.Min(1.0, (x - TimelinePad) / (double)Math.Max(1, Width - TimelinePad * 2)));
            return viewStartMs + (int)(ratio * (viewEndMs - viewStartMs));
        }

        public bool IsSeekLane(int y)
        {
            return y >= SeekRect.Top - 6 && y <= SeekRect.Bottom + 10;
        }

        public int GetKeyLane(int y)
        {
            Rectangle key1 = Key1Rect;
            Rectangle key2 = Key2Rect;
            if (y >= key1.Top && y <= key1.Bottom) return 1;
            if (y >= key2.Top && y <= key2.Bottom) return 2;
            return 0;
        }

        private Rectangle SeekRect
        {
            get
            {
                return new Rectangle(TimelinePad, 13, Math.Max(1, Width - TimelinePad * 2), 5);
            }
        }

        private Rectangle Key1Rect
        {
            get
            {
                int laneHeight = LaneHeight;
                return new Rectangle(TimelinePad, 34, Math.Max(1, Width - TimelinePad * 2), laneHeight);
            }
        }

        private Rectangle HitObjectRect
        {
            get
            {
                Rectangle k1 = Key1Rect;
                return new Rectangle(k1.Left, k1.Bottom + LaneGap, k1.Width, k1.Height);
            }
        }

        private Rectangle Key2Rect
        {
            get
            {
                Rectangle hitObject = HitObjectRect;
                return new Rectangle(hitObject.Left, hitObject.Bottom + LaneGap, hitObject.Width, hitObject.Height);
            }
        }

        private int LaneHeight
        {
            get { return Math.Max(16, (Height - 56) / 3); }
        }

        private int LaneGap
        {
            get { return Math.Max(5, Math.Min(8, Height / 18)); }
        }

        private void Timeline_Paint(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent?.BackColor ?? Color.FromArgb(10, 10, 11));

            Rectangle bounds = new Rectangle(0, 0, Math.Max(1, Width - 1), Math.Max(1, Height - 1));
            FillRoundedRectangle(g, backgroundBrush, bounds, 8);

            DrawSeek(g);
            DrawKeyLane(g, Key1Rect, "K1", Key1Mask, key1Brush);
            DrawHitObjectLane(g, HitObjectRect);
            DrawKeyLane(g, Key2Rect, "K2", Key2Mask, key2Brush);
            DrawPlayhead(g);
        }

        private void DrawSeek(Graphics g)
        {
            Rectangle seek = SeekRect;
            FillRoundedRectangle(g, seekRailBrush, seek, 3);

            int playheadX = TimeToX(viewStartMs + (int)(Value * (viewEndMs - viewStartMs)), seek.Left, seek.Width);
            if (playheadX > seek.Left)
            {
                FillRoundedRectangle(g, seekFillBrush, new Rectangle(seek.Left, seek.Top, playheadX - seek.Left, seek.Height), 3);
            }
        }

        private void DrawKeyLane(Graphics g, Rectangle lane, string label, int mask, Brush activeBrush)
        {
            if (lane.Width <= 0 || lane.Height <= 0) return;

            FillRoundedRectangle(g, laneBrush, lane, 6);

            Rectangle clip = new Rectangle(lane.Left + 4, lane.Top + 4, Math.Max(1, lane.Width - 8), Math.Max(1, lane.Height - 8));
            foreach (Rectangle segment in KeySegments(mask, clip))
            {
                g.FillRectangle(activeBrush, segment);
            }

            g.DrawString(label, Font, textBrush, lane.Left + 8, lane.Top + (lane.Height - Font.Height) / 2);

            using (Pen subtlePen = new Pen(Color.FromArgb(46, 46, 48), 1f))
            {
                g.DrawLine(subtlePen, lane.Left + 6, lane.Top, lane.Right - 6, lane.Top);
            }
        }

        private void DrawHitObjectLane(Graphics g, Rectangle lane)
        {
            if (lane.Width <= 0 || lane.Height <= 0) return;

            FillRoundedRectangle(g, laneBrush, lane, 6);

            Rectangle clip = new Rectangle(lane.Left + 5, lane.Top + 4, Math.Max(1, lane.Width - 10), Math.Max(1, lane.Height - 8));
            foreach (HitObjectView hitObject in hitObjects)
            {
                if (hitObject.EndMs < viewStartMs || hitObject.StartMs > viewEndMs)
                    continue;

                if (hitObject.Kind == 2 || hitObject.Kind == 3)
                {
                    int x1 = TimeToX(hitObject.StartMs, clip.Left, clip.Width);
                    int x2 = TimeToX(Math.Max(hitObject.EndMs, hitObject.StartMs + 1), clip.Left, clip.Width);
                    if (x2 <= x1) x2 = x1 + 2;

                    x1 = Math.Max(clip.Left, Math.Min(clip.Right, x1));
                    x2 = Math.Max(clip.Left, Math.Min(clip.Right, x2));

                    int barHeight = Math.Max(5, Math.Min(10, clip.Height / 2));
                    Rectangle bar = new Rectangle(x1, clip.Top + (clip.Height - barHeight) / 2, Math.Max(2, x2 - x1), barHeight);
                    FillRoundedRectangle(g, hitObject.Kind == 2 ? noteSliderBrush : noteSpinnerBrush, bar, Math.Max(2, barHeight / 2));
                }
                else
                {
                    int x = TimeToX(hitObject.StartMs, clip.Left, clip.Width);
                    int radius = Math.Max(3, Math.Min(6, clip.Height / 3));
                    Rectangle circle = new Rectangle(x - radius, clip.Top + clip.Height / 2 - radius, radius * 2, radius * 2);
                    g.FillEllipse(noteCircleBrush, circle);
                    g.DrawEllipse(noteOutlinePen, circle);
                }
            }

            using (Pen subtlePen = new Pen(Color.FromArgb(46, 46, 48), 1f))
            {
                g.DrawLine(subtlePen, lane.Left + 6, lane.Top, lane.Right - 6, lane.Top);
            }
        }

        private IEnumerable<Rectangle> KeySegments(int mask, Rectangle clip)
        {
            int? activeStartMs = null;
            int activeEndMs = viewStartMs;

            for (int i = 0; i < frames.Count; ++i)
            {
                int startMs = frames[i].TimeMs;
                int endMs = i + 1 < frames.Count ? frames[i + 1].TimeMs : startMs + 16;
                if (endMs < viewStartMs || startMs > viewEndMs)
                    continue;

                bool pressed = (frames[i].Keys & mask) != 0;
                if (pressed)
                {
                    if (!activeStartMs.HasValue)
                        activeStartMs = startMs;
                    activeEndMs = endMs;
                }
                else if (activeStartMs.HasValue)
                {
                    yield return KeySegmentToRectangle(activeStartMs.Value, activeEndMs, clip);
                    activeStartMs = null;
                }
            }

            if (activeStartMs.HasValue)
                yield return KeySegmentToRectangle(activeStartMs.Value, activeEndMs, clip);
        }

        private Rectangle KeySegmentToRectangle(int startMs, int endMs, Rectangle clip)
        {
            int x1 = TimeToX(startMs, clip.Left, clip.Width);
            int x2 = TimeToX(endMs, clip.Left, clip.Width);
            if (x2 <= x1) x2 = x1 + 1;

            x1 = Math.Max(clip.Left, Math.Min(clip.Right, x1));
            x2 = Math.Max(clip.Left, Math.Min(clip.Right, x2));
            return new Rectangle(x1, clip.Top, Math.Max(1, x2 - x1), clip.Height);
        }

        private void DrawPlayhead(Graphics g)
        {
            int x = TimeToX(viewStartMs + (int)(Value * (viewEndMs - viewStartMs)), TimelinePad, Math.Max(1, Width - TimelinePad * 2));
            g.DrawLine(dividerPen, x, 24, x, Height - 7);

            Point[] triangle =
            {
                new Point(x, 26),
                new Point(x - 5, 20),
                new Point(x + 5, 20),
            };
            g.FillPolygon(playheadBrush, triangle);
        }

        private int TimeToX(int timeMs, int left, int width)
        {
            double ratio = Math.Max(0.0, Math.Min(1.0, (timeMs - viewStartMs) / (double)Math.Max(1, viewEndMs - viewStartMs)));
            return left + (int)(ratio * width + 0.5);
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
