namespace AlignFace
{
    partial class Form1
    {
        /// <summary>
        ///  Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        ///  Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        ///  Required method for Designer support - do not modify
        ///  the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            button_Prev = new Button();
            button_Next = new Button();
            pictureBox_Photo = new PictureBox();
            statusStrip1 = new StatusStrip();
            toolStripStatusLabel1 = new ToolStripStatusLabel();
            ((System.ComponentModel.ISupportInitialize)pictureBox_Photo).BeginInit();
            statusStrip1.SuspendLayout();
            SuspendLayout();
            // 
            // button_Prev
            // 
            button_Prev.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
            button_Prev.Location = new Point(708, 761);
            button_Prev.Name = "button_Prev";
            button_Prev.Size = new Size(75, 23);
            button_Prev.TabIndex = 0;
            button_Prev.Text = "上一个";
            button_Prev.UseVisualStyleBackColor = true;
            button_Prev.Click += button_Prev_Click;
            // 
            // button_Next
            // 
            button_Next.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
            button_Next.Location = new Point(789, 761);
            button_Next.Name = "button_Next";
            button_Next.Size = new Size(75, 23);
            button_Next.TabIndex = 1;
            button_Next.Text = "下一个";
            button_Next.UseVisualStyleBackColor = true;
            button_Next.Click += button_Next_Click;
            // 
            // pictureBox_Photo
            // 
            pictureBox_Photo.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            pictureBox_Photo.Location = new Point(12, 12);
            pictureBox_Photo.Name = "pictureBox_Photo";
            pictureBox_Photo.Size = new Size(852, 743);
            pictureBox_Photo.SizeMode = PictureBoxSizeMode.Zoom;
            pictureBox_Photo.TabIndex = 2;
            pictureBox_Photo.TabStop = false;
            pictureBox_Photo.Paint += pictureBox_Photo_Paint;
            // 
            // statusStrip1
            // 
            statusStrip1.Items.AddRange(new ToolStripItem[] { toolStripStatusLabel1 });
            statusStrip1.Location = new Point(0, 787);
            statusStrip1.Name = "statusStrip1";
            statusStrip1.Size = new Size(876, 22);
            statusStrip1.TabIndex = 3;
            // 
            // toolStripStatusLabel1
            // 
            toolStripStatusLabel1.Name = "toolStripStatusLabel1";
            toolStripStatusLabel1.Size = new Size(75, 17);
            toolStripStatusLabel1.Text = "Hello world";
            // 
            // Form1
            // 
            AutoScaleDimensions = new SizeF(7F, 17F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(876, 809);
            Controls.Add(statusStrip1);
            Controls.Add(pictureBox_Photo);
            Controls.Add(button_Next);
            Controls.Add(button_Prev);
            Name = "Form1";
            Text = "Form1";
            FormClosing += Form1_FormClosing;
            ((System.ComponentModel.ISupportInitialize)pictureBox_Photo).EndInit();
            statusStrip1.ResumeLayout(false);
            statusStrip1.PerformLayout();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private Button button_Prev;
        private Button button_Next;
        private PictureBox pictureBox_Photo;
        private StatusStrip statusStrip1;
        private ToolStripStatusLabel toolStripStatusLabel1;
    }
}
