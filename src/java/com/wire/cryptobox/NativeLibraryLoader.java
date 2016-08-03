package com.wire.cryptobox;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Path;

public class NativeLibraryLoader {

    private static File tempDir = null;

    public static void loadLibrary(String libname) {
        if (NativeLibraryLoader.class.getResource("/fatjar") != null) {
            loadFormJar(libname);
        } else {
            System.loadLibrary(libname);
        }
    }

    private static void loadFormJar(String libname) {
        String filename = System.mapLibraryName(libname);

        try {
            if (tempDir == null) {
                tempDir = createTempDirectory("native");
            }
            File tempFile = new File(tempDir.getAbsolutePath(), filename);

            InputStream istream = CryptoBox.class.getResourceAsStream("/" + filename);
            if (istream == null) {
                throw new FileNotFoundException("Could not find native library " + filename + " in jar.");
            }

            byte[] buffer = new byte[1024];
            int bytes;
            OutputStream ostream = new FileOutputStream(tempFile);
            try {
                while ((bytes = istream.read(buffer)) != -1) {
                    ostream.write(buffer, 0, bytes);
                }
            } finally {
                ostream.close();
                istream.close();
            }

            System.load(tempFile.getAbsolutePath());
        } catch (IOException ie) {
            throw new RuntimeException(ie);
        }
    }

    private static File createTempDirectory(String dirname) throws IOException {
        final File temp;
        temp = File.createTempFile(dirname, Long.toString(System.nanoTime()));
        if (!(temp.delete())) {
            throw new IOException("Could not delete temp file: " + temp.getAbsolutePath());
        }
        if (!(temp.mkdir())) {
            throw new IOException("Could not create temp directory: " + temp.getAbsolutePath());
        }
        return (temp);
    }
}
