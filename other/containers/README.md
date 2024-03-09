The file structure follows docker standards, and every container can be created
by a Dockerfile that sits in a directory is named after the container's name.
The reasoning behind this structure is allowing syntax highlighting for all
IDEs. See: https://stackoverflow.com/questions/27409761

To build and tag an image from the root folder of the project, just use

```bash
docker build -t <username>/<container_name>:<version> \
other/containers/<container_name>
```

Then, to actually run the image in a container in interactive mode use

```bash
docker run -it <username>/<container_name> bash
```

