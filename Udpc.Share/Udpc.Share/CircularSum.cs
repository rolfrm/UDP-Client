using System;

namespace Udpc.Share
{
    
    public class CircularSum
    {
        public double Sum;
        readonly double[] buffer;
        public int Count => count;
        int count;
        int front = -1;
        public CircularSum(int count)
        {
            buffer = new double[count];
        }

        public double Last()
        {
            return buffer[front];
        }

        public double First()
        {
            int pos = front + 1;
            if (pos == count)
                pos = 0;
            return buffer[pos];
        }

        public void Add(double value)
        {
            if (count < buffer.Length)
            {
                buffer[count] = value;
                count += 1;
                front += 1;
                Sum += value;
            }
            else
            {
                front = front + 1;
                if (front >= count)
                    front = 0;
                Sum = Sum - buffer[front];
                buffer[front] = value;
                Sum += value;
            }

            //Console.WriteLine("-- {0}", front);
        }        
    }
}