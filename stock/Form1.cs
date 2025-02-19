using System.Text;

namespace stock
{
    public partial class Form1 : Form
    {
        float min = float.MaxValue;
        float max = float.MinValue;

        private static readonly HttpClient client = new HttpClient()
        {
            Timeout = TimeSpan.FromSeconds(10)
        };

        public Form1()
        {
            InitializeComponent();

            // 注册编码提供程序
            System.Text.Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
            ReadData();
        }

        private void timer1_Tick(object sender, EventArgs e)
        {
            if (IsMarketOpen())
            {
                SetTimeInterval(30 * 1000); // 30秒
                // 在这里执行更新逻辑
                Task.Run(() =>
                {
                    ReadData();
                });
            }
            else
            {
                SetTimeInterval(10 * 60 * 1000); // 十分钟
            }
        }

        private void SetTimeInterval(int val)
        {
            if(timer1.Interval != val)
                timer1.Interval = val;
        }

        private bool IsMarketOpen()
        {
            DateTime now = DateTime.Now;
            // 假设股市开市时间为上午9:30到下午4:00
            TimeSpan openTime = new TimeSpan(9, 30, 0);
            TimeSpan closeTime = new TimeSpan(16, 0, 0);

            // 判断当前时间是否在开市时间内
            return now.TimeOfDay >= openTime && now.TimeOfDay <= closeTime;
        }

        private async Task ReadData()
        {
            string stockCode = "hk00700"; // 腾讯股票的代码
            string url = $"https://qt.gtimg.cn/q={stockCode}";

            try
            {
                using (Stream stream = await client.GetStreamAsync(url))
                using (StreamReader reader = new StreamReader(stream, Encoding.GetEncoding("GB2312")))
                {
                    string response = await reader.ReadToEndAsync();
                    //string response = await client.get(url);
                    string[] data = response.Split('~');

                    if (data.Length > 1)
                    {
                        string stockName = data[1]; // 股票名称
                        string currentPrice = data[3]; // 当前价格
                        string change = data[4]; // 涨跌额
                        string changePercent = data[5]; // 涨跌幅

                        //Console.WriteLine($"股票名称: {stockName}");
                        //Console.WriteLine($"当前价格: {currentPrice}");
                        //Console.WriteLine($"涨跌额: {change}");
                        //Console.WriteLine($"涨跌幅: {changePercent}");
                        Invoke(() =>
                        {
                            label_Name.Text = stockName;
                            label_Price.Text = currentPrice;
                            label_Change.Text = change;
                            label_ChangePercent.Text = changePercent;

                            float price = float.Parse(currentPrice);
                            max = Math.Max(price, max);
                            min = Math.Min(price, min);

                            label_Min.Text = min.ToString();
                            label_Max.Text = max.ToString();
                            label_DataTime.Text = $"数据时间：{data[30]}";
                            
                            label_UpdateTime.Text = "更新时间: " + DateTime.Now.ToString("HH:mm:ss");
                        });
                    }
                    else
                    {
                        Console.WriteLine("未找到股票数据。");
                    }
                }
            }
            catch (HttpRequestException e)
            {
                Console.WriteLine($"请求错误: {e.Message}");
            }
            catch (InvalidOperationException e)
            {
                Console.WriteLine($"操作错误: {e.Message}");
            }
            catch (ArgumentException e)
            {
                Console.WriteLine($"参数错误: {e.Message}");
            }
        }
    }
}
