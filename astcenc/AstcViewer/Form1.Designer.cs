namespace AstcViewer
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
            splitContainer1 = new SplitContainer();
            pictureBox_Origin = new PictureBox();
            pictureBox_Astc = new PictureBox();
            ((System.ComponentModel.ISupportInitialize)splitContainer1).BeginInit();
            splitContainer1.Panel1.SuspendLayout();
            splitContainer1.Panel2.SuspendLayout();
            splitContainer1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)pictureBox_Origin).BeginInit();
            ((System.ComponentModel.ISupportInitialize)pictureBox_Astc).BeginInit();
            SuspendLayout();
            // 
            // splitContainer1
            // 
            splitContainer1.Dock = DockStyle.Fill;
            splitContainer1.Location = new Point(0, 0);
            splitContainer1.Name = "splitContainer1";
            // 
            // splitContainer1.Panel1
            // 
            splitContainer1.Panel1.Controls.Add(pictureBox_Origin);
            // 
            // splitContainer1.Panel2
            // 
            splitContainer1.Panel2.Controls.Add(pictureBox_Astc);
            splitContainer1.Size = new Size(1048, 833);
            splitContainer1.SplitterDistance = 540;
            splitContainer1.TabIndex = 0;
            // 
            // pictureBox_Origin
            // 
            pictureBox_Origin.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            pictureBox_Origin.Location = new Point(0, 0);
            pictureBox_Origin.Name = "pictureBox_Origin";
            pictureBox_Origin.Size = new Size(537, 833);
            pictureBox_Origin.SizeMode = PictureBoxSizeMode.Zoom;
            pictureBox_Origin.TabIndex = 0;
            pictureBox_Origin.TabStop = false;
            // 
            // pictureBox_Astc
            // 
            pictureBox_Astc.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            pictureBox_Astc.Location = new Point(3, 3);
            pictureBox_Astc.Name = "pictureBox_Astc";
            pictureBox_Astc.Size = new Size(498, 830);
            pictureBox_Astc.SizeMode = PictureBoxSizeMode.Zoom;
            pictureBox_Astc.TabIndex = 0;
            pictureBox_Astc.TabStop = false;
            // 
            // Form1
            // 
            AllowDrop = true;
            AutoScaleDimensions = new SizeF(11F, 24F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(1048, 833);
            Controls.Add(splitContainer1);
            Name = "Form1";
            Text = "Test Astc";
            FormClosing += Form1_FormClosing;
            Load += Form1_Load;
            DragDrop += Form1_DragDrop;
            DragEnter += Form1_DragEnter;
            splitContainer1.Panel1.ResumeLayout(false);
            splitContainer1.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)splitContainer1).EndInit();
            splitContainer1.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)pictureBox_Origin).EndInit();
            ((System.ComponentModel.ISupportInitialize)pictureBox_Astc).EndInit();
            ResumeLayout(false);
        }

        #endregion

        private SplitContainer splitContainer1;
        private PictureBox pictureBox_Origin;
        private PictureBox pictureBox_Astc;
    }
}
