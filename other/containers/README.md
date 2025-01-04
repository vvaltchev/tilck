The file structure follows docker standards, and every container can be created
by a Dockerfile that sits in a directory is named after the container's name.
The reasoning behind this structure is allowing syntax highlighting for all
IDEs. See: https://stackoverflow.com/questions/27409761

To build and tag an image, enter project's root folder an run:

```bash
docker build -t <user>/<name>:<ver> -f other/containers/<name>/Dockerfile .
```

To actually run the image in a container in interactive mode use:

```bash
docker run -it <user>/name>:<ver> bash
```

With Docker's cloud build, we can do:

```bash
docker buildx build --builder <builder-name> --push -t <user>/<name>:<ver> -f ./other/containers/<name>/Dockerfile .
```

