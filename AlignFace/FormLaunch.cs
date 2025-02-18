using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace AlignFace
{
    public partial class FormLaunch : Form
    {
        public FormLaunch()
        {
            InitializeComponent();
        }

        private void button_FacePicture_Click(object sender, EventArgs e)
        {
            Form1 form1 = new Form1();
            //Visible = false;
            form1.ShowDialog();
        }

        private void button_WebCamera_Click(object sender, EventArgs e)
        {
            Form_WebCamera form1 = new Form_WebCamera();
            //Visible = false;
            form1.ShowDialog();
        }
    }
}
