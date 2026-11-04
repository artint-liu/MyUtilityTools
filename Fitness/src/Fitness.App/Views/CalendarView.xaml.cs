using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Fitness.App;
using Fitness.Core.Models;
using Fitness.Core.Services;

namespace Fitness.App.Views;

public sealed class DaySessionEntry
{
    public string ProjectName { get; set; } = "";
    public int SetCount { get; set; }
    public int TotalReps { get; set; }
    public string SetSummary => string.Join(", ", Sets.Select(s => s.Repetitions));
    public List<TrainingSet> Sets { get; set; } = new();
}

public partial class CalendarView : UserControl
{
    private DateTime _month = new(DateTime.Today.Year, DateTime.Today.Month, 1);
    private Dictionary<DateTime, List<TrainingSession>> _byDate = new();
    private Dictionary<string, string> _projectNames = new();

    public CalendarView()
    {
        InitializeComponent();
        Loaded += (_, _) => BuildCalendar();
    }

    private void ReloadData()
    {
        var sessions = AppServices.Repository.LoadSessions();
        _byDate = sessions
            .GroupBy(s => s.Date.Date)
            .ToDictionary(g => g.Key, g => g.ToList());

        _projectNames = AppServices.Repository.LoadProjects()
            .ToDictionary(p => p.Id, p => p.Name);
    }

    private void BuildCalendar()
    {
        ReloadData();
        TxtMonth.Text = _month.ToString("yyyy 年 M 月");
        CalGrid.Children.Clear();

        int daysInMonth = DateTime.DaysInMonth(_month.Year, _month.Month);
        int offset = (int)new DateTime(_month.Year, _month.Month, 1).DayOfWeek; // 0=周日
        int cells = (int)Math.Ceiling((offset + daysInMonth) / 7.0) * 7;

        for (int i = 0; i < cells; i++)
        {
            int day = i - offset + 1;
            if (day < 1 || day > daysInMonth)
            {
                CalGrid.Children.Add(new Border { Background = Brushes.Transparent });
                continue;
            }

            var date = new DateTime(_month.Year, _month.Month, day);
            int total = _byDate.TryGetValue(date, out var list) ? list.Sum(s => s.TotalReps) : 0;
            bool hasData = total > 0;
            bool isToday = date.Date == DateTime.Today;

            var btn = new Button
            {
                Margin = new Thickness(2),
                Background = new SolidColorBrush(hasData
                    ? Color.FromRgb(0x2d, 0x6e, 0xa5)
                    : (isToday ? Color.FromRgb(0x3a, 0x3a, 0x4c) : Color.FromRgb(0x22, 0x22, 0x30))),
                BorderBrush = isToday ? Brushes.Gold : new SolidColorBrush(Color.FromRgb(0x44, 0x44, 0x55)),
                BorderThickness = isToday ? new Thickness(2) : new Thickness(1),
                Foreground = Brushes.White,
                Tag = date
            };

            var sp = new StackPanel { Margin = new Thickness(4) };
            sp.Children.Add(new TextBlock
            {
                Text = day.ToString(),
                FontWeight = FontWeights.Bold,
                HorizontalAlignment = HorizontalAlignment.Left
            });
            sp.Children.Add(new TextBlock
            {
                Text = hasData ? $"{total} 次" : "",
                FontSize = 11,
                Foreground = hasData ? new SolidColorBrush(Color.FromRgb(0xcf, 0xe8, 0xff)) : Brushes.Transparent,
                HorizontalAlignment = HorizontalAlignment.Left
            });
            btn.Content = sp;
            btn.Click += Day_Click;

            CalGrid.Children.Add(btn);
        }
    }

    private void Day_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: DateTime date })
            return;

        TxtDayTitle.Text = $"{date:yyyy 年 M 月 d 日} 的训练记录";
        LstDay.ItemsSource = null;

        if (!_byDate.TryGetValue(date, out var sessions))
        {
            LstDay.ItemsSource = new List<DaySessionEntry>();
            return;
        }

        var entries = sessions.Select(s => new DaySessionEntry
        {
            ProjectName = _projectNames.TryGetValue(s.ProjectId, out var n) ? n : "(已删除项目)",
            SetCount = s.Sets.Count,
            TotalReps = s.TotalReps,
            Sets = s.Sets
        }).ToList();

        LstDay.ItemsSource = entries;
    }

    private void BtnPrev_Click(object sender, RoutedEventArgs e)
    {
        _month = _month.AddMonths(-1);
        BuildCalendar();
    }

    private void BtnNext_Click(object sender, RoutedEventArgs e)
    {
        _month = _month.AddMonths(1);
        BuildCalendar();
    }

    private void BtnToday_Click(object sender, RoutedEventArgs e)
    {
        _month = new DateTime(DateTime.Today.Year, DateTime.Today.Month, 1);
        BuildCalendar();
    }
}
