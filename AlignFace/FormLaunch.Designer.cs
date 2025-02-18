namespace AlignFace
{
    partial class FormLaunch
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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(FormLaunch));
            button_FacePicture = new Button();
            button_WebCamera = new Button();
            imageList1 = new ImageList(components);
            SuspendLayout();
            // 
            // button_FacePicture
            // 
            button_FacePicture.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            button_FacePicture.ImageIndex = 0;
            button_FacePicture.ImageList = imageList1;
            button_FacePicture.Location = new Point(12, 12);
            button_FacePicture.Name = "button_FacePicture";
            button_FacePicture.Size = new Size(478, 177);
            button_FacePicture.TabIndex = 0;
            button_FacePicture.Text = "面部照片";
            button_FacePicture.TextImageRelation = TextImageRelation.ImageBeforeText;
            button_FacePicture.UseVisualStyleBackColor = true;
            button_FacePicture.Click += button_FacePicture_Click;
            // 
            // button_WebCamera
            // 
            button_WebCamera.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            button_WebCamera.ImageIndex = 1;
            button_WebCamera.ImageList = imageList1;
            button_WebCamera.Location = new Point(12, 195);
            button_WebCamera.Name = "button_WebCamera";
            button_WebCamera.Size = new Size(478, 177);
            button_WebCamera.TabIndex = 1;
            button_WebCamera.Text = "面部追踪";
            button_WebCamera.TextImageRelation = TextImageRelation.ImageBeforeText;
            button_WebCamera.UseVisualStyleBackColor = true;
            button_WebCamera.Click += button_WebCamera_Click;
            // 
            // imageList1
            // 
            imageList1.ColorDepth = ColorDepth.Depth32Bit;
            imageList1.ImageStream = (ImageListStreamer)resources.GetObject("imageList1.ImageStream");
            imageList1.TransparentColor = Color.Transparent;
            imageList1.Images.SetKeyName(0, "face-man.png");
            imageList1.Images.SetKeyName(1, "webcam.png");
            // 
            // FormLaunch
            // 
            AutoScaleDimensions = new SizeF(11F, 24F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(502, 497);
            Controls.Add(button_WebCamera);
            Controls.Add(button_FacePicture);
            Name = "FormLaunch";
            Text = "启动";
            ResumeLayout(false);
        }

        #endregion

        private Button button_FacePicture;
        private Button button_WebCamera;
        private ImageList imageList1;
    }
}