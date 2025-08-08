using ResourceChecker;
using System.Net.Sockets;
using System.Text;
// See https://aka.ms/new-console-template for more information
Console.WriteLine("Hello, World!");

RulesManager.Instance.DiscoverRules();
List<Rule> defaultRules = RulesManager.Instance.CreateDefaultRules();

foreach(var rule in defaultRules)
{
    Console.WriteLine(rule);
}
TryLoadPrefab();


void TryLoadPrefab()
{
    const string SERVER_IP = "127.0.0.1";
    const int PORT = 12345;

    try
    {
        using (TcpClient client = new TcpClient(SERVER_IP, PORT))
        using (NetworkStream stream = client.GetStream())
        {
            Console.WriteLine($"Connected to server at {SERVER_IP}:{PORT}");

            //while (true)
            {
                Console.Write("Enter command (LOADPREFAB <path> or EXIT): ");
                string input = "LOADPREFAB Assets/game/SK_M_P0000_Onesie_A0001_Show0.fbx";

                //if (input.ToUpper() == "EXIT") break;

                // 发送命令
                byte[] data = Encoding.UTF8.GetBytes(input);
                stream.Write(data, 0, data.Length);
                Console.WriteLine($"Sent command: {input}");

                // 接收响应
                byte[] buffer = new byte[4096];
                int bytesRead = stream.Read(buffer, 0, buffer.Length);
                string response = Encoding.UTF8.GetString(buffer, 0, bytesRead);

                Console.WriteLine($"\nServer response:\n{response}\n");
            }
        }
    }
    catch (Exception e)
    {
        Console.WriteLine($"Error: {e.Message}");
    }
}