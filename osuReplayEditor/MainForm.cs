using System;
using System.Windows.Forms;
using System.IO;
using System.Drawing;

namespace osuReplayEditor
{
    public partial class MainForm : Form
    {
#if DEBUG
        private const string OSR_ON_LOAD = "hhh.osr";
#endif
        private int songMin = 0;
        private int songMax = 1;
        private int editorCenterMs = 0;
        private int editorWindowDurationMs = 2000;
        private Timer FrameTimer = new Timer();
        private Timer ScrollBarTimer = new Timer();
        private MetadataEditor.MetadataEditorForm MetadataEditorForm;
        private ConfigEditor.ConfigEditorForm ConfigEditorForm;
        private bool scrubRight = false;
        private bool scrubLeft = false;
        private float scrubMult = 1.0f;
        private bool ctrlKeyDown = false;
        private bool spaceKeyDown = false;
        private bool sKeyDown = false;
        private int selectedHitObject = -1;
        private bool timelineKeyEditActive = false;
        private int timelineKeyEditMask = 0;
        private bool timelineKeyEditPressed = false;
        private int timelineKeyEditLastMs = 0;

        private bool visualMapInvert = false;
        private float originalAR = 0.0f;
        private float originalOD = 0.0f;
        private float originalCS = 0.0f;
        private JudgementTimelineControl judgementTimelineControl;
        private HitErrorBarControl hitErrorBarControl;
        private Button traceAccBtn;
        private const int TimelineKey1Mask = 5;
        private const int TimelineKey2Mask = 10;
        private const int EditorWindowMinMs = 400;
        private const int EditorWindowMaxMs = 2000;

        private bool VisualMapInvert
        {
            get => visualMapInvert;
            set
            {
                visualMapInvert = value;
                flipBeatmapBtn.Text = value ? "Un-Flip Beatmap" : "Flip Beatmap";
                API.VisualMapInvert(value);
            }
        }


        public MainForm()
        {
            InitializeComponent();
            this.judgementTimelineControl = new JudgementTimelineControl();
            this.Controls.Add(this.judgementTimelineControl);
            this.hitErrorBarControl = new HitErrorBarControl();
            this.Controls.Add(this.hitErrorBarControl);
            this.traceAccBtn = new Button
            {
                Name = "traceAccBtn",
                Text = "Trace Acc",
                UseVisualStyleBackColor = true
            };
            this.traceAccBtn.Click += analyzeAccTraceToolStripMenuItem_Click;
            this.Controls.Add(this.traceAccBtn);
            HideLegacyBottomControls();
            ApplyExpandedTimelineLayout();
            ModernTheme.Apply(this);
            this.MetadataEditorForm = new MetadataEditor.MetadataEditorForm();
            this.ConfigEditorForm = new ConfigEditor.ConfigEditorForm();
            this.ConfigEditorForm.Standalone = false;
            API.SetSkinDirectory(SkinManager.PrepareSkinDirectory());
            this.canvas.Begin();
            this.Resize += MainForm_Resize;
            this.volumeBar.Value = ReadConfiguredVolumePercent();
            this.volumeBar_ValueChanged(null, null);
            this.ChangeCursorModeNoSave(API.CfgGetCursorMode());
            this.canvas.MouseWheel += canvas_MouseWheel;
            this.timelineControl.MouseWheel += timelineControl_MouseWheel;
            this.judgementTimelineControl.MouseWheel += timelineControl_MouseWheel;
            this.timelineControl.MouseEnter += timelineControl_MouseEnter;
            this.judgementTimelineControl.MouseEnter += timelineControl_MouseEnter;
            this.timelineControl.MouseDown += timelineControl_MouseDown;
            this.timelineControl.MouseUp += timelineControl_MouseUp;
            this.judgementTimelineControl.MouseDown += judgementTimelineControl_MouseDown;
            this.judgementTimelineControl.MouseMove += judgementTimelineControl_MouseMove;
            this.MetadataEditorForm.FormClosed += MetadataEditorForm_FormClosed;
        }

        private void HideLegacyBottomControls()
        {
            Control[] controls =
            {
                markerInfoLabel, markInBtn, markOutBtn, markMidBtn, markAllBtn, clearMarksBtn,
                keypressInfoLabel, keyPressNoneBtn, keyPress1Btn, keyPress2Btn, keyPress12Btn, isKeyboard,
                trailLengthInfoLabel, trailLengthBar, cursorTrailValueLabel,
                label1, label2, label3, flipBeatmapBtn, flipCursorDataBtn,
                zoomPanResetBtn, zoomInBtn, zoomOutBtn,
                updateTimestampCB, label16, relaxRecalculateAllHitsBtn, relaxRecalculateHitsSelectionBtn,
                togglePauseBtn
            };

            foreach (Control control in controls)
            {
                control.Visible = false;
                control.Enabled = false;
                control.TabStop = false;
            }
        }

        private void ApplyExpandedTimelineLayout()
        {
            int contentLeft = 100;
            int margin = 12;
            int bottomTimelineHeight = 172;
            int bottomTimelineTop = Math.Max(360, this.ClientSize.Height - bottomTimelineHeight - margin);
            int overviewTop = 34;
            int overviewHeight = 70;
            int canvasTop = overviewTop + overviewHeight + 10;

            this.volumeBar.SetBounds(Math.Max(0, this.ClientSize.Width - 54), 54, 45, Math.Max(160, bottomTimelineTop - 66));

            int analysisLeft = Math.Max(contentLeft + 520, this.volumeBar.Left - 260);
            int contentRight = Math.Max(contentLeft + 420, analysisLeft - 12);
            int contentWidth = contentRight - contentLeft;

            this.judgementTimelineControl.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            this.judgementTimelineControl.Location = new Point(contentLeft, overviewTop);
            this.judgementTimelineControl.Name = "judgementTimelineControl";
            this.judgementTimelineControl.Size = new Size(contentWidth, overviewHeight);
            this.judgementTimelineControl.TabIndex = 81;
            this.judgementTimelineControl.TabStop = false;

            int hitErrorTop = bottomTimelineTop - 38;
            int canvasHeight = Math.Max(240, hitErrorTop - canvasTop - 8);

            this.canvas.Location = new Point(contentLeft, canvasTop);
            this.canvas.Size = new Size(contentWidth, canvasHeight);

            this.hitErrorBarControl.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
            this.hitErrorBarControl.Location = new Point(contentLeft, hitErrorTop);
            this.hitErrorBarControl.Name = "hitErrorBarControl";
            this.hitErrorBarControl.Size = new Size(contentWidth, 26);
            this.hitErrorBarControl.TabIndex = 82;
            this.hitErrorBarControl.TabStop = false;

            this.songTimeLabelLabel.Visible = false;
            this.songTimeLabel.Location = new Point(contentLeft, hitErrorTop + 29);

            this.timelineControl.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
            this.timelineControl.Location = new Point(contentLeft, bottomTimelineTop);
            this.timelineControl.Size = new Size(contentWidth, bottomTimelineHeight);
            this.timelineControl.TimelineEndMs = this.songMax;
            this.timelineControl.ViewDurationMs = editorWindowDurationMs;

            AnchorBottomRight(
                analyzeAccBtn, traceAccBtn, nextObjectBtn, nextMissBtn, next50btn, next100btn,
                label4, label5, label6, label7, label8, label9, label10, label11,
                acc_300s_tb, acc_100s_tb, acc_50s_tb, acc_misses_tb, acc_acc_tb,
                acc_avg_early_tb, acc_avg_late_tb, acc_ur_tb,
                label14, currentHitObjectIdLabel, currentHitObjectLabel,
                label15, hitObjectErrorLabel, label17, hitObjectPointsLabel);

            int analysisTop = canvasTop;
            this.analyzeAccBtn.SetBounds(analysisLeft, analysisTop, 88, 24);
            this.traceAccBtn.SetBounds(analysisLeft + 94, analysisTop, 84, 24);
            this.nextObjectBtn.SetBounds(analysisLeft + 184, analysisTop, 64, 24);
            this.nextMissBtn.SetBounds(analysisLeft, analysisTop + 31, 75, 24);
            this.next50btn.SetBounds(analysisLeft + 81, analysisTop + 31, 75, 24);
            this.next100btn.SetBounds(analysisLeft, analysisTop + 62, 75, 24);

            int statsTop = analysisTop + 96;
            this.label4.Location = new Point(analysisLeft, statsTop);
            this.acc_300s_tb.Location = new Point(analysisLeft + 50, statsTop);
            this.label5.Location = new Point(analysisLeft, statsTop + 18);
            this.acc_100s_tb.Location = new Point(analysisLeft + 50, statsTop + 18);
            this.label6.Location = new Point(analysisLeft, statsTop + 36);
            this.acc_50s_tb.Location = new Point(analysisLeft + 50, statsTop + 36);
            this.label7.Location = new Point(analysisLeft, statsTop + 54);
            this.acc_misses_tb.Location = new Point(analysisLeft + 50, statsTop + 54);
            this.label8.Location = new Point(analysisLeft, statsTop + 72);
            this.acc_acc_tb.Location = new Point(analysisLeft + 50, statsTop + 72);
            this.label17.Location = new Point(analysisLeft, statsTop + 96);
            this.hitObjectPointsLabel.Location = new Point(analysisLeft + 50, statsTop + 91);

            this.label9.Location = new Point(analysisLeft + 100, statsTop);
            this.acc_avg_early_tb.Location = new Point(analysisLeft + 162, statsTop);
            this.label10.Location = new Point(analysisLeft + 100, statsTop + 18);
            this.acc_avg_late_tb.Location = new Point(analysisLeft + 162, statsTop + 18);
            this.label11.Location = new Point(analysisLeft + 100, statsTop + 36);
            this.acc_ur_tb.Location = new Point(analysisLeft + 174, statsTop + 36);

            this.label14.Location = new Point(analysisLeft + 100, statsTop + 54);
            this.currentHitObjectIdLabel.Location = new Point(analysisLeft + 100, statsTop + 72);
            this.currentHitObjectLabel.Location = new Point(analysisLeft + 140, statsTop + 72);
            this.label15.Location = new Point(analysisLeft + 100, statsTop + 96);
            this.hitObjectErrorLabel.Location = new Point(analysisLeft + 140, statsTop + 91);
        }

        private void MainForm_Resize(object sender, EventArgs e)
        {
            ApplyExpandedTimelineLayout();
            RefreshEditorWindow(false);
        }

        private static void AnchorBottomRight(params Control[] controls)
        {
            foreach (Control control in controls)
            {
                control.Anchor = AnchorStyles.Right | AnchorStyles.Bottom;
            }
        }

        private void MetadataEditorForm_FormClosed(object sender, FormClosedEventArgs e)
        {
            UpdateModEffects();
        }

        private void UpdateModEffects()
        {
            // ---------------------------
            // DoubleTime and halftime settings:
            // ---------------------------
            if (MetadataEditorForm.IsHalfTime && MetadataEditorForm.IsDoubleTime)
            {
                ErrorMessage("This replay has incompatible mods HalfTime and DoubleTime applied.");
            }
            // Either both enabled (error state) or both disabled should be NoMod
            if (MetadataEditorForm.IsHalfTime == MetadataEditorForm.IsDoubleTime)
            {
                this.playbackRadio100x.Checked = true;
                this.ChangePlaybackSpeed();
            }
            else if (MetadataEditorForm.IsDoubleTime)
            {
                this.playbackRadio150x.Checked = true;
                this.ChangePlaybackSpeed();
            }
            else if (MetadataEditorForm.IsHalfTime)
            {
                this.playbackRadio075x.Checked = true;
                this.ChangePlaybackSpeed();
            }

            // ---------------------------
            // Easy and hardrock settings:
            // ---------------------------
            if (MetadataEditorForm.IsEasy && MetadataEditorForm.IsHardRock)
            {
                ErrorMessage("This replay has incompatible mods Easy and HardRock applied.");
            }
            // Either both enabled (error state) or both disabled should be NoMod
            if (MetadataEditorForm.IsEasy == MetadataEditorForm.IsHardRock)
            {
                VisualMapInvert = false;
                API.SetBeatmapAR(originalAR);
                API.SetBeatmapOD(originalOD);
                API.SetBeatmapCS(originalCS);
            }
            else if (MetadataEditorForm.IsHardRock)
            {
                VisualMapInvert = true;
                API.SetBeatmapAR(Math.Min(originalAR * 1.4f, 10.0f));
                API.SetBeatmapOD(Math.Min(originalOD * 1.4f, 10.0f));
                API.SetBeatmapCS(Math.Min(originalCS * 1.3f, 10.0f));
            }
            else if (MetadataEditorForm.IsEasy)
            {
                VisualMapInvert = false;
                API.SetBeatmapAR(originalAR * 0.5f);
                API.SetBeatmapOD(originalOD * 0.5f);
                API.SetBeatmapCS(originalCS * 0.5f);
            }
            UpdateHitErrorBarWindows();
            RefreshTimelineJudgementMarkers();
        }

        private static float DifficultyRange(float value, float low, float mid, float high)
        {
            if (value > 5.0f)
                return mid + (high - mid) * (value - 5.0f) / 5.0f;

            if (value < 5.0f)
                return mid - (mid - low) * (5.0f - value) / 5.0f;

            return mid;
        }

        private static float LazerHitWindow(float od, float low, float mid, float high)
        {
            return (float)Math.Floor(DifficultyRange(od, low, mid, high)) - 0.5f;
        }

        private void UpdateHitErrorBarWindows()
        {
            float od = API.GetBeatmapOD();
            this.hitErrorBarControl.SetWindows(
                LazerHitWindow(od, 80.0f, 50.0f, 20.0f),
                LazerHitWindow(od, 140.0f, 100.0f, 60.0f),
                LazerHitWindow(od, 200.0f, 150.0f, 100.0f));
        }

        private void MainForm_Load(object sender, EventArgs e)
        {
            this.FrameTimer.Interval = 15;
            this.FrameTimer.Tick += FrameTimer_Tick;
            this.FrameTimer.Start();
            this.ScrollBarTimer.Interval = 33;
            this.ScrollBarTimer.Tick += ScrollBarTimer_Tick;
            this.ScrollBarTimer.Start();
            this.updateTimestampCB.Checked = API.CfgGetUpdateTimeStampOnExit() == 1;
            this.Text += $" [{GetDllBuildLabel()}]";
#if DEBUG
            LoadReplay(OSR_ON_LOAD);
            this.Text += " (C# DEBUG)";
#endif
        }

        public void ErrorMessage(string msg)
        {
            MessageBox.Show(this, msg, "Error");
        }
        public static void ErrorMessageOwnerless(string msg)
        {
            MessageBox.Show(msg, "Error");
        }

        private string GetDllBuildLabel()
        {
            int len = 0;
            API.GetDllBuildLabel(null, ref len);
            byte[] buf = new byte[len];
            API.GetDllBuildLabel(buf, ref len);
            return System.Text.Encoding.ASCII.GetString(buf, 0, len);
        }

        private void LoadReplay(string fname)
        {
            string beatmapWarning;
            string beatmapOverride = ReplayBeatmapResolver.TryResolveBeatmapFile(fname, Config.mainConfig.OsuApiKey, out beatmapWarning);
            API.SetBeatmapFileOverride(beatmapOverride);
            if (string.IsNullOrWhiteSpace(beatmapOverride))
            {
                ErrorMessage(string.IsNullOrWhiteSpace(beatmapWarning)
                    ? "Could not resolve the .osu file for this replay from its beatmap MD5."
                    : beatmapWarning);
                return;
            }

            if (API.LoadReplay(fname))
            {
                API.Pause();
                this.songMin = API.GetReplayStartMs();
                this.songMax = API.GetReplayEndMs();
                this.timelineControl.TimelineEndMs = this.songMax;
                this.judgementTimelineControl.TimelineEndMs = this.songMax;
                editorWindowDurationMs = EditorWindowMaxMs;
                SetEditorCenter(this.songMin, true);
                this.SetPauseBtnText(false);
                this.saveFileDialog1.FileName = Path.GetFileName(fname);
                MetadataEditorForm.FromAPI();
                originalAR = API.GetBeatmapAR();
                originalOD = API.GetBeatmapOD();
                originalCS = API.GetBeatmapCS();
                UpdateModEffects();
                acc_300s_tb.Text = "?";
                acc_100s_tb.Text = "?";
                acc_50s_tb.Text = "?";
                acc_misses_tb.Text = "?";
                acc_acc_tb.Text = "?";
                acc_avg_early_tb.Text = "?";
                acc_avg_late_tb.Text = "?";
                acc_ur_tb.Text = "?";
                nextObjectBtn.Enabled = false;
                nextMissBtn.Enabled = false;
                next50btn.Enabled = false;
                next100btn.Enabled = false;
                setHitObjectInfoBlank();
                RefreshTimelineData();
            }
            else
            {
                ErrorMessage("Replay and/or song failed to load");
            }
            API.SetVolume(this.volumeBar.Value / 100.0f);
        }

        private void FrameTimer_Tick(object sender, EventArgs e)
        {
            int dt = (int)(15 * scrubMult);
            if (scrubRight)
            {
                API.RelJump(dt);
            }
            else if (scrubLeft)
            {
                API.RelJump(-dt);
            }
            this.canvas.Invalidate();
        }

        private void ScrollBarTimer_Tick(object sender, EventArgs e)
        {
            if (API.IsPlaying())
                SetEditorCenter(API.GetTime(), false);
            else
                RefreshEditorWindow(false);
        }

        private void muteBtn_Click(object sender, EventArgs e)
        {
            this.volumeBar.Value = 0;
        }

        private void volumeBar_ValueChanged(object sender, EventArgs e)
        {
            API.SetVolume(this.volumeBar.Value / 100.0f);
            this.volumeLabel.Text = $"Volume {this.volumeBar.Value}%";
            API.CfgSetVolume(this.volumeBar.Value);
        }

        private int ReadConfiguredVolumePercent()
        {
            int value = API.CfgGetVolume();
            string markerDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "osuReplayEditor");
            string markerPath = Path.Combine(markerDir, "volume-percent-v2");

            if (!File.Exists(markerPath))
            {
                if (value >= 0 && value <= 10)
                    value *= 10;

                Directory.CreateDirectory(markerDir);
                File.WriteAllText(markerPath, "ok");
                API.CfgSetVolume(value);
            }

            return Math.Max(this.volumeBar.Minimum, Math.Min(this.volumeBar.Maximum, value));
        }

        private void playbackRadio010x_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangePlaybackSpeed();
        }

        private void playbackRadio025x_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangePlaybackSpeed();
        }

        private void playbackRadio050x_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangePlaybackSpeed();
        }

        private void playbackRadio075x_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangePlaybackSpeed();
        }

        private void playbackRadio100x_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangePlaybackSpeed();
        }

        private void playbackRadio150x_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangePlaybackSpeed();
        }

        private void playbackRadio200x_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangePlaybackSpeed();
        }

        private void playbackRadio400x_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangePlaybackSpeed();
        }

        private void ChangePlaybackSpeed()
        {
            if (this.playbackRadio010x.Checked)
                scrubMult = 0.10f;
            else if (this.playbackRadio025x.Checked)
                scrubMult = 0.25f;
            else if (this.playbackRadio050x.Checked)
                scrubMult = 0.5f;
            else if (this.playbackRadio075x.Checked)
                scrubMult = 0.75f;
            else if (this.playbackRadio150x.Checked)
                scrubMult = 1.5f;
            else if (this.playbackRadio200x.Checked)
                scrubMult = 2.0f;
            else if (this.playbackRadio400x.Checked)
                scrubMult = 4.0f;
            else
                scrubMult = 1.0f;
            API.SetPlaybackSpeed(scrubMult);
        }

        private void ChangePlaybackSpeed(int percent)
        {
            throw new NotImplementedException();
        }

        private void cursorRadioNormal_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangeCursorMode();
        }

        private void cursorRadioKeys_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangeCursorMode();
        }

        private void cursorRadioPresses_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangeCursorMode();
        }

        private void ChangeCursorMode()
        {
            int kind;
            if (this.cursorRadioNormal.Checked)
                kind = 0;
            else if (this.cursorRadioKeys.Checked)
                kind = 1;
            else
                kind = 2;
            API.SetCursorTrail(kind);
            API.CfgSetCursorMode(kind);
        }

        private void ChangeCursorModeNoSave(int kind)
        {
            switch (kind)
            {
                case 0:
                    this.cursorRadioNormal.Checked = true;
                    break;
                case 1:
                    this.cursorRadioKeys.Checked = true;
                    break;
                default:
                    this.cursorRadioPresses.Checked = true;
                    break;
            }
            API.SetCursorTrail(kind);
        }

        private void timelineControl_MouseClick(object sender, MouseEventArgs e)
        {
            if (e.Button != MouseButtons.Left || !this.timelineControl.IsSeekLane(e.Y))
                return;

            JumpTimelineTo(TimelineXToMs(e.X));
        }

        private void timelineControl_MouseMove(object sender, MouseEventArgs e)
        {
            if (timelineKeyEditActive)
            {
                int ms = TimelineXToMs(e.X);
                ApplyTimelineKeyEdit(timelineKeyEditLastMs, ms);
                timelineKeyEditLastMs = ms;
            }
            else if (e.Button == MouseButtons.Left && this.timelineControl.IsSeekLane(e.Y))
            {
                JumpTimelineTo(TimelineXToMs(e.X));
            }
        }

        private void timelineControl_MouseWheel(object sender, MouseEventArgs e)
        {
            this.timelineControl.Focus();
            int step = 25;
            if ((ModifierKeys & Keys.Shift) == Keys.Shift)
                step = 5;
            if ((ModifierKeys & Keys.Control) == Keys.Control)
                step = 1;

            ShiftEditorCenter(e.Delta > 0 ? step : -step, true);
        }

        private void timelineControl_MouseEnter(object sender, EventArgs e)
        {
            this.timelineControl.Focus();
        }

        private void timelineControl_MouseDown(object sender, MouseEventArgs e)
        {
            this.timelineControl.Focus();

            if (e.Button == MouseButtons.Left && this.timelineControl.IsSeekLane(e.Y))
            {
                JumpTimelineTo(TimelineXToMs(e.X));
                return;
            }

            int lane = this.timelineControl.GetKeyLane(e.Y);
            if (lane == 0 || (e.Button != MouseButtons.Left && e.Button != MouseButtons.Right))
                return;

            API.MakeUndoSnapshot();
            timelineKeyEditActive = true;
            timelineKeyEditMask = lane == 1 ? TimelineKey1Mask : TimelineKey2Mask;
            timelineKeyEditPressed = e.Button == MouseButtons.Left;
            timelineKeyEditLastMs = TimelineXToMs(e.X);
            this.timelineControl.Capture = true;
            ApplyTimelineKeyEdit(timelineKeyEditLastMs, timelineKeyEditLastMs);
        }

        private void timelineControl_MouseUp(object sender, MouseEventArgs e)
        {
            if (!timelineKeyEditActive)
                return;

            ApplyTimelineKeyEdit(timelineKeyEditLastMs, TimelineXToMs(e.X));
            timelineKeyEditActive = false;
            timelineKeyEditMask = 0;
            this.timelineControl.Capture = false;
        }

        private void judgementTimelineControl_MouseDown(object sender, MouseEventArgs e)
        {
            if (e.Button != MouseButtons.Left)
                return;

            JumpTimelineTo(JudgementTimelineXToMs(e.X));
        }

        private void judgementTimelineControl_MouseMove(object sender, MouseEventArgs e)
        {
            if (e.Button != MouseButtons.Left)
                return;

            JumpTimelineTo(JudgementTimelineXToMs(e.X));
        }

        private int TimelineXToMs(int x)
        {
            return Math.Max(this.songMin, Math.Min(this.songMax, this.timelineControl.XToTimeMs(x)));
        }

        private int JudgementTimelineXToMs(int x)
        {
            return Math.Max(this.songMin, Math.Min(this.songMax, this.judgementTimelineControl.XToTimeMs(x)));
        }

        private void ApplyTimelineKeyEdit(int startMs, int endMs)
        {
            if (API.SetFrameKeyPressRange(startMs, endMs, timelineKeyEditMask, timelineKeyEditPressed))
            {
                RefreshTimelineReplayFrames();
                RefreshTimelineJudgementMarkers();
                RefreshHitErrorBarMarkers();
            }
        }

        private void JumpTimelineTo(int ms)
        {
            SetEditorCenter(ms, true);
        }

        private void UpdateKeyTimelineViewport(int centerMs)
        {
            SetEditorCenter(centerMs, false);
        }

        private int ClampEditorCenter(int ms)
        {
            int min = Math.Min(this.songMin, this.songMax);
            int max = Math.Max(this.songMin, this.songMax);
            return Math.Max(min, Math.Min(max, ms));
        }

        private void SetEditorCenter(int ms, bool jumpAudio)
        {
            editorCenterMs = ClampEditorCenter(ms);
            RefreshEditorWindow(jumpAudio);
        }

        private void SetEditorWindowDuration(int durationMs)
        {
            editorWindowDurationMs = Math.Max(EditorWindowMinMs, Math.Min(EditorWindowMaxMs, durationMs));
            RefreshEditorWindow(false);
        }

        private void ShiftEditorCenter(int deltaMs, bool jumpAudio)
        {
            SetEditorCenter(editorCenterMs + deltaMs, jumpAudio);
        }

        private void RefreshEditorWindow(bool jumpAudio)
        {
            editorWindowDurationMs = Math.Max(EditorWindowMinMs, Math.Min(EditorWindowMaxMs, editorWindowDurationMs));
            editorCenterMs = ClampEditorCenter(editorCenterMs);

            this.timelineControl.TimelineEndMs = this.songMax;
            this.timelineControl.ViewDurationMs = editorWindowDurationMs;
            this.timelineControl.SetViewport(editorCenterMs, this.songMin, this.songMax);

            this.judgementTimelineControl.TimelineEndMs = this.songMax;
            this.judgementTimelineControl.Value = editorCenterMs / (double)Math.Max(1, this.songMax);
            this.hitErrorBarControl.CurrentTimeMs = editorCenterMs;

            int startMs;
            int endMs;
            GetEditorWindowBounds(out startMs, out endMs);
            API.SetEditorWindow(startMs, endMs);

            if (jumpAudio)
                API.JumpTo(editorCenterMs);

            UpdateSongTimeLabel();
            RefreshOverviewSummary();
        }

        private void GetEditorWindowBounds(out int startMs, out int endMs)
        {
            int rangeMin = Math.Min(this.songMin, this.songMax);
            int rangeMax = Math.Max(this.songMin + 1, this.songMax);
            int duration = Math.Max(1, editorWindowDurationMs);

            int start = editorCenterMs - duration / 2;
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
            startMs = start;
            endMs = end;
        }

        private void UpdateSongTimeLabel()
        {
            this.songTimeLabel.Text = $"{FormatTime(editorCenterMs, true)} / {FormatTime(songMax, false)}  {editorWindowDurationMs}ms";
        }

        private void RefreshOverviewSummary()
        {
            string acc = string.IsNullOrWhiteSpace(acc_acc_tb.Text) ? "?" : acc_acc_tb.Text;
            string combo = API.Replay_GetMaxCombo().ToString();
            this.judgementTimelineControl.SummaryText =
                $"{FormatTime(editorCenterMs, false)} / {FormatTime(songMax, false)}   ACC: {acc}   x{combo}";
        }

        private static string FormatTime(int ms, bool includeMs)
        {
            ms = Math.Max(0, ms);
            int msPart = ms % 1000;
            int sec = (ms / 1000) % 60;
            int min = (ms / 1000) / 60;
            return includeMs ? $"{min:D2}:{sec:D2}:{msPart:D3}" : $"{min:D2}:{sec:D2}";
        }

        private void RefreshTimelineData()
        {
            RefreshTimelineReplayFrames();
            RefreshTimelineHitObjects();
            RefreshTimelineJudgementMarkers();
            RefreshHitErrorBarMarkers();
        }

        private void RefreshTimelineReplayFrames()
        {
            int count = API.GetReplayFrameCount();
            if (count <= 0)
            {
                this.timelineControl.SetReplayFrames(null, null, 0);
                return;
            }

            int[] times = new int[count];
            int[] keys = new int[count];
            int written = API.GetReplayFrames(times, keys, count);
            this.timelineControl.SetReplayFrames(times, keys, written);
        }

        private void RefreshTimelineHitObjects()
        {
            int count = API.GetTimelineHitObjectCount();
            if (count <= 0)
            {
                this.timelineControl.SetHitObjects(null, null, null, 0);
                return;
            }

            int[] startTimes = new int[count];
            int[] endTimes = new int[count];
            int[] kinds = new int[count];
            int written = API.GetTimelineHitObjects(startTimes, endTimes, kinds, count);
            this.timelineControl.SetHitObjects(startTimes, endTimes, kinds, written);
        }

        private void RefreshTimelineJudgementMarkers()
        {
            int count = API.GetTimelineJudgementMarkerCount();
            if (count <= 0)
            {
                this.judgementTimelineControl.SetJudgementMarkers(null, null, 0);
                RefreshOverviewSummary();
                return;
            }

            int[] times = new int[count];
            int[] kinds = new int[count];
            int written = API.GetTimelineJudgementMarkers(times, kinds, count);
            this.judgementTimelineControl.SetJudgementMarkers(times, kinds, written);
            RefreshOverviewSummary();
        }

        private void RefreshHitErrorBarMarkers()
        {
            int count = API.GetHitErrorMarkerCount();
            if (count <= 0)
            {
                this.hitErrorBarControl.SetMarkers(null, null, null, 0);
                return;
            }

            int[] times = new int[count];
            int[] errors = new int[count];
            int[] points = new int[count];
            int written = API.GetHitErrorMarkers(times, errors, points, count);
            this.hitErrorBarControl.SetMarkers(times, errors, points, written);
        }

        private void MainForm_DragEnter(object sender, DragEventArgs e)
        {
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                e.Effect = DragDropEffects.Copy;
            }
        }

        private void MainForm_DragDrop(object sender, DragEventArgs e)
        {
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                string[] files = (string[])e.Data.GetData(DataFormats.FileDrop);
                this.LoadReplay(files[0]);
            }
        }

        private void togglePauseBtn_Click(object sender, EventArgs e)
        {
            TogglePlayback();
        }

        private void SetPauseBtnText(bool isPlaying)
        {
            this.togglePauseBtn.Text = isPlaying ? "Pause" : "Play";
        }

        private void TogglePlayback()
        {
            if (API.IsPlaying())
                API.Pause();
            else
                API.Play();

            this.SetPauseBtnText(API.IsPlaying());
        }

        private void replayMetadataToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.MetadataEditorForm.FromAPI();
            this.MetadataEditorForm.ShowDialog();
        }

        private void configToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.ConfigEditorForm.ShowDialog();
        }

        private void markInBtn_Click(object sender, EventArgs e)
        {
            API.PlaceMarkIn();
        }

        private void markOutBtn_Click(object sender, EventArgs e)
        {
            API.PlaceMarkOut();
        }

        private void markMidBtn_Click(object sender, EventArgs e)
        {
            API.PlaceMarkMid();
        }

        private void markAllBtn_Click(object sender, EventArgs e)
        {
            API.PlaceMarkAll();
        }

        private void clearMarksBtn_Click(object sender, EventArgs e)
        {
            API.ClearMarks();
        }

        private void keyPressNoneBtn_Click(object sender, EventArgs e)
        {
            this.SetFrameKeyPress(0);
        }

        private void keyPress1Btn_Click(object sender, EventArgs e)
        {
            
            this.SetFrameKeyPress(this.isKeyboard.Checked ? 5 : 1);
        }

        private void keyPress2Btn_Click(object sender, EventArgs e)
        {
            this.SetFrameKeyPress(this.isKeyboard.Checked ? 10 : 2);
        }

        private void keyPress12Btn_Click(object sender, EventArgs e)
        {
            this.SetFrameKeyPress(this.isKeyboard.Checked ? 15 : 3);
        }

        private void SetFrameKeyPress(int mask)
        {
            if (!API.SetFrameKeyPress(mask))
            {
                ErrorMessage("Could not key change press data, do you have a valid selection?");
            }
            else
            {
                RefreshTimelineReplayFrames();
                RefreshTimelineJudgementMarkers();
            }
        }

        private void canvas_MouseDown(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
                API.MouseDownRight(e.X, e.Y);
            else
                API.MouseDown(e.X, e.Y);
        }

        private void canvas_MouseUp(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
                API.MouseUpRight(e.X, e.Y);
            else
            {
                API.MouseUp(e.X, e.Y);
                RefreshTimelineJudgementMarkers();
                RefreshHitErrorBarMarkers();
                RefreshTimelineReplayFrames();
            }
        }

        private void canvas_MouseMove(object sender, MouseEventArgs e)
        {
            API.MouseMove(e.X, e.Y);
        }

        private void canvas_MouseWheel(object sender, MouseEventArgs e)
        {
            API.MouseWheel(e.X, e.Y, e.Delta > 0);
        }

        private void trailLengthBar_Scroll(object sender, EventArgs e)
        {
            int ms = this.trailLengthBar.Value * 100;
            API.SetCursorTrailLength(ms);
            this.cursorTrailValueLabel.Text = ms.ToString() + " ms";
        }

        private void Hotkey(Keys key)
        {
            if (ctrlKeyDown)
            {
                switch (key)
                {
                    case Keys.Z:
                        if (API.Undo())
                            RefreshTimelineData();
                        break;
                    case Keys.Y:
                        if (API.Redo())
                            RefreshTimelineData();
                        break;
                    case Keys.Left:
                        JumpToAdjacentKeyPress(false);
                        break;
                    case Keys.Right:
                        JumpToAdjacentKeyPress(true);
                        break;
                }
            }
            else
            {
                switch (key)
                {
                    case Keys.Space:
                    case Keys.S:
                        TogglePlayback();
                        break;
                    case Keys.Q:
                        SetEditorWindowDuration(editorWindowDurationMs - 7);
                        break;
                    case Keys.E:
                        SetEditorWindowDuration(editorWindowDurationMs + 7);
                        break;
                    case Keys.A:
                    case Keys.Left:
                        ShiftEditorCenter(-4, true);
                        break;
                    case Keys.D:
                    case Keys.Right:
                        ShiftEditorCenter(4, true);
                        break;
                    case Keys.D1:
                        this.toolSelGrabRadioButton.Checked = true;
                        this.ChangeTool();
                        break;
                    case Keys.D2:
                        this.toolBrushRadioButton.Checked = true;
                        this.ChangeTool();
                        break;
                    case Keys.R:
                        this.zoomPanResetBtn_Click(null, null);
                        break;
                    case Keys.OemCloseBrackets:
                        this.incBrushRadiusTrackBarValue(5);
                        break;
                    case Keys.OemOpenBrackets:
                        this.incBrushRadiusTrackBarValue(-5);
                        break;
                    case Keys.Z:
                        this.keyPressNoneBtn_Click(null, null);
                        break;
                    case Keys.X:
                        this.keyPress1Btn_Click(null, null);
                        break;
                    case Keys.C:
                        this.keyPress2Btn_Click(null, null);
                        break;
                    case Keys.V:
                        this.keyPress12Btn_Click(null, null);
                        break;
                }
            }
        }

        private void JumpToAdjacentKeyPress(bool next)
        {
            int count = API.GetReplayFrameCount();
            if (count <= 0)
                return;

            int[] times = new int[count];
            int[] keys = new int[count];
            int written = API.GetReplayFrames(times, keys, count);
            int keyMask = TimelineKey1Mask | TimelineKey2Mask;
            int target = -1;

            for (int i = 0; i < written; ++i)
            {
                bool pressed = (keys[i] & keyMask) != 0;
                bool wasPressed = i > 0 && (keys[i - 1] & keyMask) != 0;
                if (!pressed || wasPressed)
                    continue;

                if (next)
                {
                    if (times[i] > editorCenterMs + 1)
                    {
                        target = times[i];
                        break;
                    }
                }
                else if (times[i] < editorCenterMs - 1)
                {
                    target = times[i];
                }
            }

            if (target >= 0)
                JumpTimelineTo(target);
        }

        protected override bool ProcessKeyPreview(ref Message msg)
        {
            const int WM_KEYDOWN = 0x100;
            const int WM_KEYUP = 0x101;
            if (msg.Msg != WM_KEYDOWN && msg.Msg != WM_KEYUP)
                return base.ProcessKeyPreview(ref msg);
            Keys keyData = (Keys)msg.WParam;
            if (ShouldLetFocusedControlHandleKey(keyData))
                return base.ProcessKeyPreview(ref msg);
            bool isRight = keyData == Keys.D || keyData == Keys.Right;
            bool isLeft = keyData == Keys.A || keyData == Keys.Left;
            bool isCtrl = keyData == Keys.ControlKey;
            bool isSpace = keyData == Keys.Space;
            bool isS = keyData == Keys.S;
            if (msg.Msg == WM_KEYDOWN)
            {
                if (isSpace)
                {
                    if (spaceKeyDown)
                        return true;
                    spaceKeyDown = true;
                }
                else if (isS)
                {
                    if (sKeyDown)
                        return true;
                    sKeyDown = true;
                }

                if (isCtrl)
                {
                    ctrlKeyDown = true;
                }
                this.Hotkey(keyData);
                if (isSpace || isS || isRight || isLeft)
                    return true;
            }
            else if (msg.Msg == WM_KEYUP)
            {
                if (isSpace)
                {
                    spaceKeyDown = false;
                    return true;
                }
                else if (isS)
                {
                    sKeyDown = false;
                    return true;
                }

                if (isRight || isLeft)
                {
                    return true;
                }
                else if (isCtrl)
                {
                    ctrlKeyDown = false;
                }
            }
            return base.ProcessKeyPreview(ref msg);
        }

        private bool ShouldLetFocusedControlHandleKey(Keys key)
        {
            if (key != Keys.Space && key != Keys.S)
                return false;

            Control control = this.ActiveControl;
            while (control is ContainerControl container && container.ActiveControl != null)
            {
                control = container.ActiveControl;
            }

            return control is TextBoxBase || control is ComboBox || control is NumericUpDown;
        }

        private void openToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.openFileDialog1.ShowDialog();
        }

        private void openFileDialog1_FileOk(object sender, System.ComponentModel.CancelEventArgs e)
        {
            this.LoadReplay(this.openFileDialog1.FileName);
        }

        private void saveFile()
        {
            this.saveFileDialog1.ShowDialog();
        }

        private void saveToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.saveFile();
        }

        private void exportAsosrToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.saveFileDialog1.ShowDialog();
        }

        private void saveFileDialog1_FileOk(object sender, System.ComponentModel.CancelEventArgs e)
        {
            if (API.CfgGetUpdateTimeStampOnExit() == 1)
            {
                MetadataEditorForm.PlayTimestamp = DateTime.Now;
                MetadataEditorForm.ToAPI();
                MetadataEditorForm.FromAPI();
                UpdateModEffects();
            }

            var metadataChoice = MessageBox.Show(
                this,
                "Apply recalculated lazer judgement metadata before export?\n\nYes = update hit counts, max combo, and score\nNo = keep the current replay metadata\nCancel = stop export",
                "Confirm Export Metadata",
                MessageBoxButtons.YesNoCancel,
                MessageBoxIcon.Question);

            if (metadataChoice == DialogResult.Cancel)
            {
                e.Cancel = true;
                return;
            }

            if (metadataChoice == DialogResult.Yes)
            {
                if (API.JudgementHasUnsupportedMods())
                {
                    MessageBox.Show(
                        this,
                        "This replay uses a non-standard mode or unsupported mods. Only osu!standard NM, HR, and DT are supported for lazer judgement metadata.\n\nExport will continue without metadata overwrite.",
                        "Unsupported Mods",
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Warning);
                }
                else if (!API.AnalyzeAndApplyReplayMetadata())
                {
                    MessageBox.Show(
                        this,
                        "Could not apply recalculated judgement metadata. Export will continue with the current replay metadata.",
                        "Metadata Not Updated",
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Warning);
                }
                else
                {
                    MetadataEditorForm.FromAPI();
                }
            }

            API.ExportAsOsr(this.saveFileDialog1.FileName);
        }

        private void githubToolStripMenuItem1_Click(object sender, EventArgs e)
        {
            System.Diagnostics.Process.Start("https://github.com/thebetioplane/OsuReplayEditorV3");
        }

        private void flipBeatmapBtn_Click(object sender, EventArgs e)
        {
            VisualMapInvert = !VisualMapInvert;
        }

        private void flipCursorDataBtn_Click(object sender, EventArgs e)
        {
            API.InvertCursorData();
        }

        private void zoomPanResetBtn_Click(object sender, EventArgs e)
        {
            API.ResetPanZoom();
        }

        private void zoomInBtn_Click(object sender, EventArgs e)
        {
            API.ZoomIn();
        }

        private void zoomOutBtn_Click(object sender, EventArgs e)
        {
            API.ZoomOut();
        }

        private bool UserWantsToExit()
        {
            var result = MessageBox.Show(this, "Are you sure you want to exit?\nAll unsaved changes will be lost.", "Confirm Exit", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
            return result == DialogResult.Yes;
        }

        private void MainForm_FormClosing(object sender, FormClosingEventArgs e)
        {
            if (UserWantsToExit())
            {
                API.Cleanup();
            }
            else
            {
                e.Cancel = true;
            }
        }

        private void updateTimestampCB_CheckedChanged(object sender, EventArgs e)
        {
            int value = updateTimestampCB.Checked ? 1 : 0;
            API.CfgSetUpdateTimeStampOnExit(value);
        }

        private void undoToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (API.Undo())
                RefreshTimelineData();
        }

        private void redoToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (API.Redo())
                RefreshTimelineData();
        }

        private void keybindReferenceToolStripMenuItem_Click(object sender, EventArgs e)
        {
            MessageBox.Show("Space / S - toggle play / pause from current time\nA/D or Left/Right - nudge time window\nCtrl+Left/Right - jump to previous/next key input\nQ/E - shrink/grow time window\nTimeline wheel - nudge 25 ms\nShift + timeline wheel - nudge 5 ms\nCtrl + timeline wheel - nudge 1 ms\n1 - grab/select tool\n2 - brush tool\nCanvas mouse wheel - zoom\nRight click - pan\nR - reset zoom pan\nCtrl+Z - undo\nCtrl+Y - redo\n[ - decrease brush size\n] - increase brush size", "Keybind Reference", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        private string getKindString(int kind)
        {
            switch (kind)
            {
                case 1:
                    return "Slider";
                case 2:
                    return "Spinner";
                case 3:
                    return "Slider Tick";
                case 4:
                    return "Slider End";
                default:
                    return "Circle";
            }
        }

        private void analyzeAccuracy(bool doTrace)
        {
            int num300 = 0, num100 = 0, num50 = 0, numMiss = 0;
            float accuracy = 0.0f, avgEarly = 0.0f, avgLate = 0.0f, unstableRate = 0.0f;
            API.AnalyzeAccuracy(doTrace, ref num300, ref num100, ref num50, ref numMiss, ref accuracy, ref avgEarly, ref avgLate, ref unstableRate);
            acc_300s_tb.Text = num300.ToString();
            acc_100s_tb.Text = num100.ToString();
            acc_50s_tb.Text = num50.ToString();
            acc_misses_tb.Text = numMiss.ToString();
            double displayedAccuracy = Math.Floor(Math.Max(0.0, accuracy) * 100.0) / 100.0;
            acc_acc_tb.Text = $"{displayedAccuracy:0.00}%";
            acc_avg_early_tb.Text = $"{avgEarly:0.##}";
            acc_avg_late_tb.Text = $"{avgLate:0.##}";
            acc_ur_tb.Text = $"{unstableRate:0.##}";
            nextObjectBtn.Enabled = true;
            nextMissBtn.Enabled = true;
            next50btn.Enabled = true;
            next100btn.Enabled = true;
            setHitObjectInfoIndex(selectedHitObject);
            RefreshTimelineJudgementMarkers();
            RefreshHitErrorBarMarkers();
            RefreshOverviewSummary();
        }

        private void analyzeAccBtn_Click(object sender, EventArgs e)
        {
            analyzeAccuracy(false);
        }

        private void openAccuracyTraceLog()
        {
            string[] candidates =
            {
                Path.Combine(Environment.CurrentDirectory, "accuracy_analyzer.log"),
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "accuracy_analyzer.log")
            };

            foreach (string path in candidates)
            {
                if (!File.Exists(path)) continue;
                try
                {
                    System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
                    {
                        FileName = "notepad.exe",
                        Arguments = $"\"{path}\"",
                        UseShellExecute = false
                    });
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Trace log was written, but could not be opened:\n{path}\n\n{ex.Message}",
                        "Analyze Accuracy Trace", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
                return;
            }

            MessageBox.Show("Trace log was not found. Try running Analyze Acc Trace again.",
                "Analyze Accuracy Trace", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private void setHitObjectInfo(int index, int kind, bool isMiss, int hitError, int points)
        {
            if (index < 0 || kind == -1)
            {
                setHitObjectInfoBlank();
            }
            else
            {
                currentHitObjectIdLabel.Text = $"#{index}";
                currentHitObjectLabel.Text = getKindString(kind);
                if (isMiss)
                {
                    hitObjectErrorLabel.Text = (kind == 3 || kind == 4) ? "Sliderbreak (Miss)" : "Miss";
                }
                else
                {
                    string hitErrorQualifier = (hitError > 0) ? "Late" : (hitError < 0) ? "Early" : "Perfect";
                    hitObjectErrorLabel.Text = $"{hitError} ms ({hitErrorQualifier})";
                }
                hitObjectPointsLabel.Text = points.ToString();
            }
        }
        private void setHitObjectInfoBlank()
        {
            currentHitObjectLabel.Text = "?";
            hitObjectErrorLabel.Text = "?";
            hitObjectPointsLabel.Text = "?";
        }
        private bool setHitObjectInfoIndex(int index)
        {
            selectedHitObject = index;
            if (index < 0)
            {
                setHitObjectInfoBlank();
                return false;
            }
            int kind = -1;
            bool isMiss = false;
            int hitError = 0;
            int points = 0;
            if (!API.GetHitInfo(index, ref kind, ref isMiss, ref hitError, ref points))
            {
                setHitObjectInfoBlank();
                return false;
            }
            setHitObjectInfo(index, kind, isMiss, hitError, points);
            return true;
        }
        private bool NavigateToJudgement(int index)
        {
            if (!setHitObjectInfoIndex(index))
                return false;

            SetEditorCenter(API.GetTime(), false);
            return true;
        }
        private void nextMissBtn_Click(object sender, EventArgs e)
        {
            if (!NavigateToJudgement(API.NextMiss()))
                MessageBox.Show("No more misses");
        }
        private void next50btn_Click(object sender, EventArgs e)
        {
            if (!NavigateToJudgement(API.Next50()))
                MessageBox.Show("No more 50s");
        }
        private void next100btn_Click(object sender, EventArgs e)
        {
            if (!NavigateToJudgement(API.Next100()))
                MessageBox.Show("No more 100s");
        }

        private void nextObjectBtn_Click(object sender, EventArgs e)
        {
            if (!NavigateToJudgement(API.NextHitObject()))
                MessageBox.Show("No more hit objects");
        }

        private void toolSelGrabRadioButton_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangeTool();
        }

        private void toolBrushRadioButton_CheckedChanged(object sender, EventArgs e)
        {
            this.ChangeTool();
        }

        private void ChangeTool()
        {
            int whichTool;
            if (toolSelGrabRadioButton.Checked)
                whichTool = 0;
            else
                whichTool = 2;
            API.SetTool(whichTool);
        }

        private void brushRadiusTrackBar_ValueChanged(object sender, EventArgs e)
        {
            this.brushRadiusLabel.Text = this.brushRadiusTrackBar.Value.ToString();
            API.SetBrushRadius(brushRadiusTrackBar.Value);
        }

        private void incBrushRadiusTrackBarValue(int amt)
        {
            int value = this.brushRadiusTrackBar.Value + amt;
            int low = this.brushRadiusTrackBar.Minimum;
            int high = this.brushRadiusTrackBar.Maximum;
            if (value < low)
                value = low;
            else if (value > high)
                value = high;
            this.brushRadiusTrackBar.Value = value;
        }

        private void analyzeAccTraceToolStripMenuItem_Click(object sender, EventArgs e)
        {
#if DEBUG
            analyzeAccuracy(true);
            openAccuracyTraceLog();
#else
            var res = MessageBox.Show("This feature produces a log file with details on the decisions that the accuracy analyzer made. This is useful for reporting issues.\n\nWould you like to continue?", "Analyze Accuracy Trace", MessageBoxButtons.YesNoCancel);                Console.WriteLine(res);
            if (res == DialogResult.Yes)
            {
                analyzeAccuracy(true);
                openAccuracyTraceLog();
            }
#endif
        }

        private void relaxRecalculateAllHitsBtn_Click(object sender, EventArgs e)
        {
            API.RelaxRecalculateAllHits();
            RefreshTimelineData();
        }

        private void relaxRecalculateHitsSelectionBtn_Click(object sender, EventArgs e)
        {
            API.RelaxRecalculateHitsInSelection();
            RefreshTimelineData();
        }
    }
}
