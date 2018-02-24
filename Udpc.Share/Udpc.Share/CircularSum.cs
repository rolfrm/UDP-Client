namespace Udpc.Share
{
    public class CircularSum
    {
        public double Sum;
        readonly double[] buffer;
        public int Count { get; private set; }

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
            if (pos == Count)
                pos = 0;
            return buffer[pos];
        }

        public void Add(double value)
        {
            if (Count < buffer.Length)
            {
                buffer[Count] = value;
                Count += 1;
                front += 1;
                Sum += value;
            }
            else
            {
                front = front + 1;
                if (front >= Count)
                    front = 0;
                Sum = Sum - buffer[front];
                buffer[front] = value;
                Sum += value;
            }
        }        
    }
}