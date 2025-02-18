namespace AlignFace
{
    partial class Form_WebCamera
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
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
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            components = new System.ComponentModel.Container();
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(Form_WebCamera));
            pictureBox_Video = new PictureBox();
            button_Start = new Button();
            imageList1 = new ImageList(components);
            button_Stop = new Button();
            ((System.ComponentModel.ISupportInitialize)pictureBox_Video).BeginInit();
            SuspendLayout();
            // 
            // pictureBox_Video
            // 
            pictureBox_Video.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            pictureBox_Video.BackgroundImage = Properties.Resources.video_box;
            pictureBox_Video.BackgroundImageLayout = ImageLayout.Zoom;
            pictureBox_Video.ErrorImage = Properties.Resources.video_box;
            pictureBox_Video.InitialImage = Properties.Resources.video_box;
            pictureBox_Video.Location = new Point(12, 12);
            pictureBox_Video.Name = "pictureBox_Video";
            pictureBox_Video.Size = new Size(936, 762);
            pictureBox_Video.SizeMode = PictureBoxSizeMode.Zoom;
            pictureBox_Video.TabIndex = 0;
            pictureBox_Video.TabStop = false;
            // 
            // button_Start
            // 
            button_Start.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
            button_Start.ImageIndex = 0;
            button_Start.ImageList = imageList1;
            button_Start.Location = new Point(718, 804);
            button_Start.Name = "button_Start";
            button_Start.Size = new Size(112, 40);
            button_Start.TabIndex = 1;
            button_Start.Text = "start";
            button_Start.TextImageRelation = TextImageRelation.ImageBeforeText;
            button_Start.UseVisualStyleBackColor = true;
            button_Start.Click += button_Start_Click;
            // 
            // imageList1
            // 
            imageList1.ColorDepth = ColorDepth.Depth32Bit;
            imageList1.ImageStream = (ImageListStreamer)resources.GetObject("imageList1.ImageStream");
            imageList1.TransparentColor = Color.Transparent;
            imageList1.Images.SetKeyName(0, "play.png");
            imageList1.Images.SetKeyName(1, "stop.png");
            // 
            // button_Stop
            // 
            button_Stop.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
            button_Stop.Enabled = false;
            button_Stop.ImageIndex = 1;
            button_Stop.ImageList = imageList1;
            button_Stop.Location = new Point(836, 804);
            button_Stop.Name = "button_Stop";
            button_Stop.Size = new Size(112, 40);
            button_Stop.TabIndex = 2;
            button_Stop.Text = "stop";
            button_Stop.TextImageRelation = TextImageRelation.ImageBeforeText;
            button_Stop.UseVisualStyleBackColor = true;
            button_Stop.Click += button_Stop_Click;
            // 
            // Form_WebCamera
            // 
            AutoScaleDimensions = new SizeF(11F, 24F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(960, 856);
            Controls.Add(button_Stop);
            Controls.Add(button_Start);
            Controls.Add(pictureBox_Video);
            Name = "Form_WebCamera";
            Text = "Form_WebCamera";
            FormClosing += Form_WebCamera_FormClosing;
            FormClosed += Form_WebCamera_FormClosed;
            ((System.ComponentModel.ISupportInitialize)pictureBox_Video).EndInit();
            ResumeLayout(false);
        }

        #endregion

        private PictureBox pictureBox_Video;
        private Button button_Start;
        private Button button_Stop;
        private ImageList imageList1;
    }
}