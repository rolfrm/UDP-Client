using System;
using System.Linq;
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

    public class ObjectContainer
    {
        public object Value;
        public override string ToString()
        {
            return string.Format("{0}", Value);
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

            var lst = new ListBox();
            lst.Items = new byte[] {1, 2, 34, 5,5,5,5,5, 6, 7, 8, 9, 9, 8, 7, 6, 5, 4}.Select(x => new ObjectContainer(){Value =  x}).ToList();
            
            stk.Children.Add(lst);

            scrl.Content = stk;

            Content = scrl;
        }

        void OnClicked(object sender, RoutedEventArgs routedEventArgs)
        {
            Console.WriteLine("Button clicked");
        }
    }
}