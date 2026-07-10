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
        private const int TimelineInset = 16;
        private const int LowerTimelineHeight = 172;
        private const int Key1ZoneY = 30;
        private const int Key2ZoneY = 105;
        private const int ZoneHeight = 45;
        private const int ZoneRadius = 15;
        private const int KeyInputTopOffset = 6;
        private const int KeyInputHeight = 32;
        private const int KeyInputRadius = 5;
        private const int HitObjectCenterY = 86;
        private const int HitObjectHeight = 10;
        private const int PlayheadTriangleBaseY = 10;
        private const int PlayheadTriangleTipY = 20;
        private const int PlayheadLineTop = 30;
        private const int PlayheadLineBottom = 145;

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

        private readonly Brush backgroundBrush = new SolidBrush(Color.FromArgb(23, 23, 23));
        private readonly Brush laneBrush = new SolidBrush(Color.FromArgb(40, 40, 40));
        private readonly Brush key1Brush = new SolidBrush(Color.FromArgb(187, 107, 217));
        private readonly Brush key2Brush = new SolidBrush(Color.FromArgb(242, 153, 74));
        private readonly Brush hitObjectBrush = new SolidBrush(Color.FromArgb(51, 255, 255, 255));
        private readonly Brush playheadBrush = new SolidBrush(Color.White);
        private readonly Pen playheadPen = new Pen(Color.White, 2.5f);

        public TimelineControl() : base()
        {
            this.Value = 0;
            this.TimelineEndMs = 1;
            this.ViewDurationMs = 2000;
            this.TabStop = true;
            this.MinimumSize = new Size(TimelineInset * 2, LowerTimelineHeight);
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

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                backgroundBrush.Dispose();
                laneBrush.Dispose();
                key1Brush.Dispose();
                key2Brush.Dispose();
                hitObjectBrush.Dispose();
                playheadBrush.Dispose();
                playheadPen.Dispose();
            }

            base.Dispose(disposing);
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
            double ratio = Math.Max(0.0, Math.Min(1.0, (x - TimelineLeft) / (double)TimelineWidth));
            return viewStartMs + (int)(ratio * (viewEndMs - viewStartMs));
        }

        public bool IsSeekLane(int y)
        {
            return false;
        }

        public int GetKeyLane(int y)
        {
            Rectangle key1 = Key1Rect;
            Rectangle key2 = Key2Rect;
            if (y >= key1.Top && y <= key1.Bottom) return 1;
            if (y >= key2.Top && y <= key2.Bottom) return 2;
            return 0;
        }

        private Rectangle Key1Rect
        {
            get
            {
                return new Rectangle(TimelineLeft, Key1ZoneY, TimelineWidth, ZoneHeight);
            }
        }

        private Rectangle Key2Rect
        {
            get
            {
                return new Rectangle(TimelineLeft, Key2ZoneY, TimelineWidth, ZoneHeight);
            }
        }

        private int TimelineLeft
        {
            get { return Width > TimelineInset * 2 ? TimelineInset : 0; }
        }

        private int TimelineRight
        {
            get { return Width > TimelineInset * 2 ? Width - TimelineInset : Math.Max(0, Width - 1); }
        }

        private int TimelineWidth
        {
            get { return Math.Max(1, TimelineRight - TimelineLeft); }
        }

        private void Timeline_Paint(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent?.BackColor ?? Color.FromArgb(10, 10, 11));

            Rectangle bounds = new Rectangle(0, 0, Math.Max(1, Width), Math.Max(1, Height));
            g.FillRectangle(backgroundBrush, bounds);

            DrawKeyLane(g, Key1Rect, Key1Mask, key1Brush);
            DrawHitObjects(g);
            DrawKeyLane(g, Key2Rect, Key2Mask, key2Brush);
            DrawPlayhead(g);
        }

        private void DrawKeyLane(Graphics g, Rectangle lane, int mask, Brush activeBrush)
        {
            if (lane.Width <= 0 || lane.Height <= 0) return;

            FillRoundedRectangle(g, laneBrush, lane, ZoneRadius);

            Rectangle clip = new Rectangle(
                lane.Left,
                lane.Top + KeyInputTopOffset,
                lane.Width,
                KeyInputHeight);

            foreach (Rectangle segment in KeySegments(mask, clip))
            {
                FillRoundedRectangle(g, activeBrush, segment, KeyInputRadius);
            }
        }

        private void DrawHitObjects(Graphics g)
        {
            int top = HitObjectCenterY - HitObjectHeight / 2;
            int left = TimelineLeft;
            int width = TimelineWidth;
            int right = TimelineRight;

            foreach (HitObjectView hitObject in hitObjects)
            {
                if (hitObject.EndMs < viewStartMs || hitObject.StartMs > viewEndMs)
                    continue;

                if ((hitObject.Kind == 2 || hitObject.Kind == 3) && hitObject.EndMs > hitObject.StartMs)
                {
                    int x1 = TimeToX(hitObject.StartMs, left, width);
                    int x2 = TimeToX(hitObject.EndMs, left, width);

                    x1 = Math.Max(left, Math.Min(right, x1));
                    x2 = Math.Max(left, Math.Min(right, x2));
                    if (x2 <= x1) x2 = x1 + 2;

                    Rectangle bar = new Rectangle(x1, top, Math.Max(2, x2 - x1), HitObjectHeight);
                    FillRoundedRectangle(g, hitObjectBrush, bar, HitObjectHeight / 2);
                }
                else
                {
                    int x = TimeToX(hitObject.StartMs, left, width);
                    Rectangle marker = new Rectangle(x - HitObjectHeight / 2, top, HitObjectHeight, HitObjectHeight);
                    FillRoundedRectangle(g, hitObjectBrush, marker, HitObjectHeight / 2);
                }
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
            int x = Math.Max(0, Width / 2);
            int lineBottom = Math.Min(PlayheadLineBottom, Math.Max(PlayheadLineTop, Height - 1));
            g.DrawLine(playheadPen, x, PlayheadLineTop, x, lineBottom);

            Point[] triangle =
            {
                new Point(x - 6, PlayheadTriangleBaseY),
                new Point(x + 6, PlayheadTriangleBaseY),
                new Point(x, PlayheadTriangleTipY),
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
