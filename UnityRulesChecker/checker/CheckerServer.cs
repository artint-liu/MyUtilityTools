using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEditor;
using UnityEngine;

public class CheckerServer : EditorWindow
{
    private TcpListener server;
    private Thread listenerThread;
    private bool isRunning = false;
    private int port = 12346;
    private string log = "Server Log:\n";
    private Vector2 scrollPosition;

    private readonly Queue<Action> mainThreadActions = new Queue<Action>();
    private readonly object queueLock = new object();

    private static SynchronizationContext mainThreadSyncContext;


    private void OnEnable()
    {
        mainThreadSyncContext = SynchronizationContext.Current;
    }

    private void OnDisable()
    {

    }

    public static void RunInMainThread(System.Action action)
    {
        mainThreadSyncContext.Post(_ => action(), null);
    }

    [MenuItem("工具/CheckerServer TCP")]
    public static void ShowWindow()
    {
        GetWindow<CheckerServer>("TCP Server");
    }

    //[MenuItem("工具/T")]
    //{
    //}

    void OnGUI()
    {
        GUILayout.Label("TCP Server Settings", EditorStyles.boldLabel);

        EditorGUILayout.Space();
        port = EditorGUILayout.IntField("Port", port);

        EditorGUILayout.Space();
        if (GUILayout.Button(isRunning ? "Stop Server" : "Start Server"))
        {
            if (isRunning) StopServer();
            else StartServer();
        }

        //EditorGUILayout.Space();
        //GUILayout.Label("Server Log", EditorStyles.boldLabel);

        //scrollPosition = EditorGUILayout.BeginScrollView(scrollPosition, GUILayout.Height(400));
        //EditorGUILayout.TextArea(log, GUILayout.ExpandHeight(true));
        //EditorGUILayout.EndScrollView();

        //if (GUILayout.Button("Clear Log"))
        //{
        //    log = "";
        //    //log = "Server Log:\n";
        //}
    }

    private void StartServer()
    {
        if (isRunning) return;

        try
        {
            server = new TcpListener(IPAddress.Any, port);
            server.Start();

            listenerThread = new Thread(ListenForClients);
            listenerThread.IsBackground = true;
            listenerThread.Start();

            isRunning = true;
            LogMessage($"Server started on port {port}");
        }
        catch (Exception e)
        {
            LogMessage($"Error starting server: {e.Message}");
        }
    }

    private void StopServer()
    {
        if (!isRunning) return;

        try
        {
            server.Stop();
            listenerThread.Abort();
            isRunning = false;
            LogMessage("Server stopped");
        }
        catch (Exception e)
        {
            LogMessage($"Error stopping server: {e.Message}");
        }
    }

    private void ListenForClients()
    {
        while (isRunning)
        {
            try
            {
                TcpClient client = server.AcceptTcpClient();
                Thread clientThread = new Thread(() => HandleClientComm(client));
                clientThread.IsBackground = true;
                clientThread.Start();

                LogMessage($"Client connected: {client.Client.RemoteEndPoint}");
            }
            catch (Exception e)
            {
                if (isRunning) LogMessage($"Error accepting client: {e.Message}");
            }
        }
    }

    private void HandleClientComm(TcpClient client)
    {
        using (NetworkStream stream = client.GetStream())
        {
            byte[] buffer = new byte[1024];
            int bytesRead;

            try
            {
                while ((bytesRead = stream.Read(buffer, 0, buffer.Length)) != 0)
                {
                    string command = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                    LogMessage($"Received command: {command}");

                    // Queue command processing on main thread
                    lock (queueLock)
                    {
                        mainThreadActions.Enqueue(() => ProcessCommand(command, stream));
                    }
                }
            }
            catch (Exception e)
            {
                LogMessage($"Client error: {e.Message}");
            }
            finally
            {
                client.Close();
                LogMessage($"Client disconnected");
            }
        }
    }

    private void ProcessCommand(string command, NetworkStream stream)
    {
        if (command.StartsWith("LOADPREFAB "))
        {
            string prefabPath = command.Substring(11);
            LoadPrefab(prefabPath, stream);
        }
        else
        {
            SendResponse("ERROR: Unknown command", stream);
        }
    }

    private void LoadPrefab(string path, NetworkStream stream)
    {
        try
        {
            // 验证路径安全性
            if (path.Contains("..") || path.Contains("//") || path.Contains("\\"))
            {
                SendResponse("ERROR: Invalid path format", stream);
                return;
            }

            // 尝试加载Prefab
            GameObject prefab = AssetDatabase.LoadAssetAtPath<GameObject>(path);
            if (prefab == null)
            {
                SendResponse($"ERROR: Prefab not found at {path}", stream);
                return;
            }

            // 在场景中创建Prefab实例
            GameObject instance = (GameObject)PrefabUtility.InstantiatePrefab(prefab);
            instance.name = $"{prefab.name}_Instance";

            // 收集属性信息
            string response = $"SUCCESS: Prefab loaded\n" +
                            $"Name: {instance.name}\n" +
                            $"Position: {instance.transform.position}\n" +
                            $"Rotation: {instance.transform.rotation.eulerAngles}\n" +
                            $"Scale: {instance.transform.localScale}\n" +
                            $"Components: {GetComponentList(instance)}";

            SendResponse(response, stream);
            LogMessage($"Loaded prefab: {path}");
        }
        catch (Exception e)
        {
            SendResponse($"ERROR: {e.Message}", stream);
            LogMessage($"Error loading prefab: {e.Message}");
        }
    }

    private string GetComponentList(GameObject go)
    {
        List<string> components = new List<string>();
        foreach (Component comp in go.GetComponents<Component>())
        {
            components.Add(comp.GetType().Name);
        }
        return string.Join(", ", components);
    }

    private void SendResponse(string message, NetworkStream stream)
    {
        try
        {
            byte[] responseBytes = Encoding.UTF8.GetBytes(message);
            stream.Write(responseBytes, 0, responseBytes.Length);
            LogMessage($"Sent response: {message}");
        }
        catch (Exception e)
        {
            LogMessage($"Error sending response: {e.Message}");
        }
    }

    private void LogMessage(string message)
    {
        //log += $"[{DateTime.Now:HH:mm:ss}] {message}\n";
        RunInMainThread(() =>
        {
            Debug.Log(message);
            //Repaint(); // 刷新窗口显示
        });
    }

    private void Update()
    {
        // Process queued actions on main thread
        lock (queueLock)
        {
            while (mainThreadActions.Count > 0)
            {
                mainThreadActions.Dequeue().Invoke();
            }
        }
    }

    void OnDestroy()
    {
        if (isRunning) StopServer();
    }
}