namespace stock
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
            components = new System.ComponentModel.Container();
            timer1 = new System.Windows.Forms.Timer(components);
            label1 = new Label();
            label2 = new Label();
            label3 = new Label();
            label4 = new Label();
            label_Name = new Label();
            label_Price = new Label();
            label_Change = new Label();
            label_ChangePercent = new Label();
            label5 = new Label();
            label6 = new Label();
            label_Max = new Label();
            label_Min = new Label();
            SuspendLayout();
            // 
            // timer1
            // 
            timer1.Enabled = true;
            timer1.Interval = 10000;
            timer1.Tick += timer1_Tick;
            // 
            // label1
            // 
            label1.AutoSize = true;
            label1.Location = new Point(12, 9);
            label1.Name = "label1";
            label1.Size = new Size(82, 24);
            label1.TabIndex = 0;
            label1.Text = "股票名称";
            // 
            // label2
            // 
            label2.AutoSize = true;
            label2.Location = new Point(12, 33);
            label2.Name = "label2";
            label2.Size = new Size(82, 24);
            label2.TabIndex = 1;
            label2.Text = "当前价格";
            // 
            // label3
            // 
            label3.AutoSize = true;
            label3.Location = new Point(12, 57);
            label3.Name = "label3";
            label3.Size = new Size(64, 24);
            label3.TabIndex = 2;
            label3.Text = "涨跌额";
            // 
            // label4
            // 
            label4.AutoSize = true;
            label4.Location = new Point(12, 81);
            label4.Name = "label4";
            label4.Size = new Size(64, 24);
            label4.TabIndex = 3;
            label4.Text = "涨跌幅";
            // 
            // label_Name
            // 
            label_Name.AutoSize = true;
            label_Name.Location = new Point(100, 9);
            label_Name.Name = "label_Name";
            label_Name.Size = new Size(58, 24);
            label_Name.TabIndex = 4;
            label_Name.Text = "name";
            // 
            // label_Price
            // 
            label_Price.AutoSize = true;
            label_Price.Location = new Point(100, 33);
            label_Price.Name = "label_Price";
            label_Price.Size = new Size(53, 24);
            label_Price.TabIndex = 5;
            label_Price.Text = "price";
            // 
            // label_Change
            // 
            label_Change.AutoSize = true;
            label_Change.Location = new Point(100, 57);
            label_Change.Name = "label_Change";
            label_Change.Size = new Size(73, 24);
            label_Change.TabIndex = 6;
            label_Change.Text = "change";
            // 
            // label_ChangePercent
            // 
            label_ChangePercent.AutoSize = true;
            label_ChangePercent.Location = new Point(100, 81);
            label_ChangePercent.Name = "label_ChangePercent";
            label_ChangePercent.Size = new Size(138, 24);
            label_ChangePercent.TabIndex = 7;
            label_ChangePercent.Text = "changePercent";
            // 
            // label5
            // 
            label5.AutoSize = true;
            label5.Location = new Point(13, 105);
            label5.Name = "label5";
            label5.Size = new Size(46, 24);
            label5.TabIndex = 8;
            label5.Text = "最大";
            // 
            // label6
            // 
            label6.AutoSize = true;
            label6.Location = new Point(13, 129);
            label6.Name = "label6";
            label6.Size = new Size(46, 24);
            label6.TabIndex = 9;
            label6.Text = "最小";
            // 
            // label_Max
            // 
            label_Max.AutoSize = true;
            label_Max.Location = new Point(100, 105);
            label_Max.Name = "label_Max";
            label_Max.Size = new Size(47, 24);
            label_Max.TabIndex = 10;
            label_Max.Text = "Max";
            // 
            // label_Min
            // 
            label_Min.AutoSize = true;
            label_Min.Location = new Point(100, 129);
            label_Min.Name = "label_Min";
            label_Min.Size = new Size(44, 24);
            label_Min.TabIndex = 11;
            label_Min.Text = "Min";
            // 
            // Form1
            // 
            AutoScaleDimensions = new SizeF(11F, 24F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(302, 164);
            Controls.Add(label_Min);
            Controls.Add(label_Max);
            Controls.Add(label6);
            Controls.Add(label5);
            Controls.Add(label_ChangePercent);
            Controls.Add(label_Change);
            Controls.Add(label_Price);
            Controls.Add(label_Name);
            Controls.Add(label4);
            Controls.Add(label3);
            Controls.Add(label2);
            Controls.Add(label1);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            MinimizeBox = false;
            Name = "Form1";
            SizeGripStyle = SizeGripStyle.Hide;
            Text = "股票";
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private System.Windows.Forms.Timer timer1;
        private Label label1;
        private Label label2;
        private Label label3;
        private Label label4;
        private Label label_Name;
        private Label label_Price;
        private Label label_Change;
        private Label label_ChangePercent;
        private Label label5;
        private Label label6;
        private Label label_Max;
        private Label label_Min;
    }
}
