using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;

namespace Udpc.Share.Gui
{
    class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("Hello World!");
            AppBuilder.Configure<App>().UsePlatformDetect().UseReactiveUI().Start<MainWindow>();
        }
    }
    
    public class App : Application
    {
        public override void Initialize()
        {
            AvaloniaXamlLoader.Load(this);
        }
    }

    public class MainWindow : Window
    {
        public MainWindow()
        {
            Title = "Orbital";
            var scrl = new ScrollViewer();
            var stk = new StackPanel();
            for (int i = 0; i < 10; i++)
            {
                var btn = new Button {Content = "Hello World", Margin = new Thickness(10)};
                btn.Click += OnClicked;
                stk.Children.Add(btn);
            }

            scrl.Content = stk;

            Content = scrl;
        }

        void OnClicked(object sender, RoutedEventArgs routedEventArgs)
        {
            Console.WriteLine("Button clicked");
        }
    }
}