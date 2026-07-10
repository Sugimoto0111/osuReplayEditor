using System.Drawing;
using System.Windows.Forms;

namespace osuReplayEditor
{
    static class ModernTheme
    {
        private static readonly Color Background = Color.FromArgb(10, 10, 11);
        private static readonly Color Surface = Color.FromArgb(22, 23, 25);
        private static readonly Color Border = Color.FromArgb(54, 56, 62);
        private static readonly Color Text = Color.FromArgb(230, 233, 240);
        private static readonly Color MutedText = Color.FromArgb(170, 176, 188);

        public static void Apply(Form form)
        {
            form.Font = new Font("Segoe UI", 9F, FontStyle.Regular, GraphicsUnit.Point);
            form.BackColor = Background;

            foreach (Control control in form.Controls)
            {
                Apply(control);
            }
        }

        private static void Apply(Control control)
        {
            if (control is Canvas || control is TimelineControl || control is JudgementTimelineControl || control is HitErrorBarControl)
                return;

            control.Font = new Font("Segoe UI", control.Font.SizeInPoints, control.Font.Style, GraphicsUnit.Point);

            if (control is MenuStrip menu)
            {
                menu.BackColor = Surface;
                menu.ForeColor = Text;
                menu.RenderMode = ToolStripRenderMode.Professional;
                menu.Renderer = new ToolStripProfessionalRenderer(new ModernColorTable());
            }
            else if (control is Button button)
            {
                button.FlatStyle = FlatStyle.Flat;
                button.BackColor = Surface;
                button.ForeColor = Text;
                button.UseVisualStyleBackColor = false;
                button.FlatAppearance.BorderColor = Border;
                button.FlatAppearance.BorderSize = 1;
                button.FlatAppearance.MouseOverBackColor = Color.FromArgb(36, 38, 43);
                button.FlatAppearance.MouseDownBackColor = Color.FromArgb(48, 50, 56);
            }
            else if (control is TextBox textBox)
            {
                textBox.BorderStyle = BorderStyle.FixedSingle;
                textBox.BackColor = Surface;
                textBox.ForeColor = Text;
            }
            else if (control is Label label)
            {
                label.ForeColor = label.Font.SizeInPoints >= 12F ? Text : MutedText;
            }
            else if (control is CheckBox checkBox)
            {
                checkBox.BackColor = Background;
                checkBox.ForeColor = Text;
                checkBox.UseVisualStyleBackColor = false;
            }
            else if (control is RadioButton radioButton)
            {
                radioButton.BackColor = Background;
                radioButton.ForeColor = Text;
                radioButton.UseVisualStyleBackColor = false;
            }
            else if (control is Panel || control is TrackBar)
            {
                control.BackColor = Background;
                control.ForeColor = Text;
            }

            foreach (Control child in control.Controls)
            {
                Apply(child);
            }
        }

        private sealed class ModernColorTable : ProfessionalColorTable
        {
            public override Color ToolStripGradientBegin => Surface;
            public override Color ToolStripGradientMiddle => Surface;
            public override Color ToolStripGradientEnd => Surface;
            public override Color MenuStripGradientBegin => Surface;
            public override Color MenuStripGradientEnd => Surface;
            public override Color MenuItemSelected => Color.FromArgb(36, 38, 43);
            public override Color MenuItemSelectedGradientBegin => Color.FromArgb(36, 38, 43);
            public override Color MenuItemSelectedGradientEnd => Color.FromArgb(36, 38, 43);
            public override Color MenuItemPressedGradientBegin => Color.FromArgb(48, 50, 56);
            public override Color MenuItemPressedGradientEnd => Color.FromArgb(48, 50, 56);
            public override Color MenuBorder => Border;
            public override Color MenuItemBorder => Color.FromArgb(72, 76, 86);
            public override Color ImageMarginGradientBegin => Surface;
            public override Color ImageMarginGradientMiddle => Surface;
            public override Color ImageMarginGradientEnd => Surface;
            public override Color SeparatorDark => Border;
            public override Color SeparatorLight => Surface;
        }
    }
}
