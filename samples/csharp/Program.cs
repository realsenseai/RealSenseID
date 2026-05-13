using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

using rsid;
namespace ConsoleApp1
{
    class Program
    {

        // Authentication callbacks
        static void OnAuthHint(rsid.AuthStatus hint, float frameScore, IntPtr ctx)
        {
            Console.WriteLine("OnHint " + hint);
        }

        static void OnAuthResult(rsid.AuthStatus status, string userId, short score, IntPtr ctx)
        {
            Console.WriteLine("OnResults " + status);
            if (status == AuthStatus.Success)
            {
                Console.WriteLine("Authenticated " + userId);
            }
        }

        static void OnFaceDetected(IntPtr facesArr, int faceCount, uint timestamp, IntPtr ctx)
        {
            Console.WriteLine($"OnFaceDetected: {faceCount} face(s)");
            //convert to face rects
            var faces = rsid.Authenticator.MarshalFaces(facesArr, faceCount);            
            foreach(var face in faces)
            {
                Console.WriteLine($"*** OnFaceDetected {face.x},{face.y}, {face.width}x{face.height} (ts {timestamp})");
            }
        }

        static void OnLandmarksDetected(IntPtr landmarksArr, int faceCount, uint timestamp, IntPtr ctx)
        {
            Console.WriteLine($"OnLandmarksDetected: {faceCount} face(s)");
            //convert to face landmarks
            var faces = rsid.Authenticator.MarshalFaces(landmarksArr, faceCount);            
            foreach(var face in faces)
            {
                Console.WriteLine($"*** OnLandmarksDetected {face.x},{face.y}, {face.width}x{face.height} (ts {timestamp})");
            }
        }

        static void OnPersonDetected(IntPtr personsArr, int personCount, uint timestamp, IntPtr ctx)
        {
            Console.WriteLine($"OnPersonDetected: {personCount} person(s)");
            //convert to person rects
            var persons = rsid.Authenticator.MarshalPersons(personsArr, personCount);
            foreach (var person in persons)
            {
                Console.WriteLine($"*** OnPersonDetected (id {person.id}) {person.x},{person.y}, {person.width}x{person.height} (ts {timestamp}) score={person.score:F2}");
            }
        }

        static void OnBarcodeDecoded(IntPtr barcodesArr, int barcodeCount, uint timestamp, IntPtr ctx)
        {
            Console.WriteLine($"OnBarcodeDecoded: {barcodeCount} barcode(s)");
            //convert to barcode strings
            var barcodes = rsid.Authenticator.MarshalBarcodes(barcodesArr, barcodeCount);
            foreach (var barcode in barcodes)
            {
                Console.WriteLine($"*** OnBarcodeDecoded: {barcode} (ts {timestamp})");
            }
        }

        static void Main(string[] args)
        {
            var devices = rsid.Discover.DiscoverDevices();
            if (devices.Length == 0)
            {
                Console.WriteLine("No device detected");
                return;
            }
            var device = devices[0];
            Console.WriteLine("Using device on port " + device.SerialPort);

            var auth = new rsid.Authenticator();
            if (auth.Connect(new SerialConfig { port = device.SerialPort }) != Status.Ok)
            {
                Console.WriteLine("Error connecting to device");
                return;
            }
            var authArgs = new AuthArgs { hintClbk = OnAuthHint, resultClbk = OnAuthResult, faceDetectedClbk = OnFaceDetected};
            auth.Authenticate(authArgs);
        }
    }
}
