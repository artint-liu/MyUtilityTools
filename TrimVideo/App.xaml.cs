using System.Windows;

namespace TrimVideo
{
    public partial class App : Application
    {
        private void App_OnStartup(object sender, StartupEventArgs e)
        {
            var mainWindow = new MainWindow();

            if (e.Args.Length > 0)
            {
                string filePath = e.Args[0];
                mainWindow.SetInitialFile(filePath);
            }

            mainWindow.Show();
        }
    }
}
