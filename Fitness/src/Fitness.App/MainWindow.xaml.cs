using System.Windows;
using System.Windows.Controls;
using Fitness.App.Views;

namespace Fitness.App;

public partial class MainWindow : Window
{
    private readonly CreateProjectView _createView = new();
    private readonly CameraTestView _cameraView = new();
    private readonly ExecuteView _executeView = new();
    private readonly CalendarView _calendarView = new();

    public MainWindow()
    {
        InitializeComponent();
        Cc.Content = _createView;
        Highlight(NavCreate);
    }

    private void NavCreate_Click(object sender, RoutedEventArgs e)
    {
        Cc.Content = _createView;
        Highlight(NavCreate);
    }

    private void NavCamera_Click(object sender, RoutedEventArgs e)
    {
        Cc.Content = _cameraView;
        Highlight(NavCamera);
    }

    private void NavExecute_Click(object sender, RoutedEventArgs e)
    {
        Cc.Content = _executeView;
        Highlight(NavExecute);
    }

    private void NavCalendar_Click(object sender, RoutedEventArgs e)
    {
        Cc.Content = _calendarView;
        Highlight(NavCalendar);
    }

    private void Highlight(Button active)
    {
        foreach (var b in new[] { NavCreate, NavCamera, NavExecute, NavCalendar })
            b.Background = b == active
                ? new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x3a, 0x6e, 0xa5))
                : new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x2a, 0x2a, 0x3c));
    }
}
