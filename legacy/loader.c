/*
 * sensor_load.c
 *
 * Stage 1 of the virtual sensor.
 * Loads a raw float32 file into memory and prints a summary.
 */

#include <stdio.h>
#include <stdlib.h>

#define BLOCK_FLOATS 16384   // 64 KiB / 4 bytes per float


int main(int argc, char **argv)
{
    // Step 1: Check whether the filename was provided
    if (argc != 2) {
        fprintf(stderr, "Usage: %s file.bin\n", argv[0]);
        return 1;
    }

    // Step 2: Open the binary file for reading
    FILE *input_file = fopen(argv[1], "rb");

    if (input_file == NULL) {
        perror("fopen");
        return 1;
    }

    // Step 3: Move to the end of the file to find its size
    if (fseek(input_file, 0, SEEK_END) != 0) {
        perror("fseek end");
        fclose(input_file);
        return 1;
    }

    // Step 4: Get the file size in bytes
    long file_bytes = ftell(input_file);

    if (file_bytes <= 0) {
        fprintf(stderr, "File is empty or ftell failed\n");
        fclose(input_file);
        return 1;
    }

    // Step 5: Return to the beginning of the file
    if (fseek(input_file, 0, SEEK_SET) != 0) {
        perror("fseek start");
        fclose(input_file);
        return 1;
    }

    // Step 6: Calculate how many float samples the file contains
    if (file_bytes % sizeof(float) != 0) {
        fprintf(stderr,
                "Warning: file size is not a multiple of 4 bytes\n");
    }

    size_t sample_count = file_bytes / sizeof(float);

    // Step 7: Allocate memory for all the samples
    float *samples = malloc(sample_count * sizeof(float));

    if (samples == NULL) {
        perror("malloc");
        fclose(input_file);
        return 1;
    }

    // Step 8: Read the samples into memory
    size_t samples_read = fread(
        samples,
        sizeof(float),
        sample_count,
        input_file
    );

    if (samples_read != sample_count) {
        fprintf(stderr,
                "Short read: got %zu of %zu samples\n",
                samples_read,
                sample_count);

        free(samples);
        fclose(input_file);
        return 1;
    }

    // Step 9: Close the file
    if (fclose(input_file) != 0) {
        perror("fclose");
        free(samples);
        return 1;
    }

    // Step 10: Find the minimum and maximum values
    float min_value = samples[0];
    float max_value = samples[0];

    for (size_t i = 1; i < sample_count; i++) {

        if (samples[i] < min_value) {
            min_value = samples[i];
        }

        if (samples[i] > max_value) {
            max_value = samples[i];
        }
    }

    // Step 11: Print the file summary
    printf("File: %s\n", argv[1]);

    printf("Samples: %zu (%ld bytes)\n",
           sample_count, file_bytes);

    printf("Full 64 KiB blocks: %zu\n",
           sample_count / BLOCK_FLOATS);

    printf("Leftover samples: %zu\n",
           sample_count % BLOCK_FLOATS);

    printf("Minimum: %f\n", min_value);
    printf("Maximum: %f\n", max_value);

    // Step 12: Print the first 10 samples
    printf("First 10 samples:\n");

    for (size_t i = 0; i < 10 && i < sample_count; i++) {
        printf("[%zu] %f\n", i, samples[i]);
    }

    // Step 13: Release the allocated memory
    free(samples);

    return 0;
}
