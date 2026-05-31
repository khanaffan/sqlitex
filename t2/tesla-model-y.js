// Tesla Model Y 3D Configurator - curved procedural mesh
(function () {
    'use strict';

    let scene, camera, renderer, controls;
    let bodyGroup, interiorGroup;
    let bodyMaterial, interiorMaterial;
    let wheelGroups = [];

    const config = {
        exteriorColor: '#e8e8e8',
        wheelType: 'gemini',
        interiorColor: '#1a1a1a'
    };

    const paintOptions = {
        metalness: 0.82,
        roughness: 0.16,
        clearcoat: 1,
        clearcoatRoughness: 0.08
    };

    function init() {
        scene = new THREE.Scene();
        scene.background = new THREE.Color(0x171725);
        scene.fog = new THREE.Fog(0x171725, 24, 48);

        camera = new THREE.PerspectiveCamera(40, window.innerWidth / window.innerHeight, 0.1, 100);
        camera.position.set(6.3, 2.8, 7.2);

        renderer = new THREE.WebGLRenderer({ antialias: true });
        renderer.setSize(window.innerWidth, window.innerHeight);
        renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        renderer.shadowMap.enabled = true;
        renderer.shadowMap.type = THREE.PCFSoftShadowMap;
        renderer.toneMapping = THREE.ACESFilmicToneMapping;
        renderer.toneMappingExposure = 1.15;
        document.getElementById('canvas-container').appendChild(renderer.domElement);

        controls = new THREE.OrbitControls(camera, renderer.domElement);
        controls.enableDamping = true;
        controls.dampingFactor = 0.05;
        controls.minDistance = 4;
        controls.maxDistance = 15;
        controls.maxPolarAngle = Math.PI / 2.05;
        controls.target.set(0, 0.85, 0);

        setupLights();
        buildGround();
        buildTeslaModelY();
        setupEventListeners();
        animate();
    }

    function setupLights() {
        scene.add(new THREE.AmbientLight(0xffffff, 0.5));

        const key = new THREE.DirectionalLight(0xffffff, 1.05);
        key.position.set(6, 10, 5);
        key.castShadow = true;
        key.shadow.mapSize.set(2048, 2048);
        key.shadow.camera.left = -9;
        key.shadow.camera.right = 9;
        key.shadow.camera.top = 9;
        key.shadow.camera.bottom = -9;
        scene.add(key);

        const side = new THREE.DirectionalLight(0x9fb8ff, 0.45);
        side.position.set(-6, 5, -5);
        scene.add(side);

        const rim = new THREE.DirectionalLight(0xffffff, 0.65);
        rim.position.set(-2, 3, -9);
        scene.add(rim);

        const dome = new THREE.Mesh(
            new THREE.SphereGeometry(36, 32, 32),
            new THREE.MeshBasicMaterial({ color: 0x262642, side: THREE.BackSide })
        );
        scene.add(dome);
    }

    function buildGround() {
        const ground = new THREE.Mesh(
            new THREE.PlaneGeometry(50, 50),
            new THREE.MeshStandardMaterial({ color: 0x10101d, roughness: 0.85, metalness: 0.15 })
        );
        ground.rotation.x = -Math.PI / 2;
        ground.receiveShadow = true;
        scene.add(ground);

        const reflection = new THREE.Mesh(
            new THREE.CircleGeometry(4.4, 96),
            new THREE.MeshStandardMaterial({ color: 0x1d1d32, roughness: 0.22, metalness: 0.75 })
        );
        reflection.rotation.x = -Math.PI / 2;
        reflection.position.y = 0.012;
        reflection.receiveShadow = true;
        scene.add(reflection);
    }

    function buildTeslaModelY() {
        bodyGroup = new THREE.Group();
        interiorGroup = new THREE.Group();

        bodyMaterial = new THREE.MeshPhysicalMaterial({
            color: new THREE.Color(config.exteriorColor),
            ...paintOptions
        });

        interiorMaterial = new THREE.MeshStandardMaterial({
            color: new THREE.Color(config.interiorColor),
            roughness: 0.78,
            metalness: 0.02
        });

        buildSmoothBodyShell();
        buildGlassCanopy();
        buildSideWindows();
        buildSignatureDetails();
        buildInterior();
        buildWheels(config.wheelType);

        scene.add(bodyGroup);
        scene.add(interiorGroup);
    }

    function createLoftGeometry(stations, radialSegments, bottomY) {
        const vertices = [];
        const normals = [];
        const indices = [];

        stations.forEach((s) => {
            for (let j = 0; j <= radialSegments; j++) {
                const t = j / radialSegments;
                const angle = Math.PI * t;
                const side = Math.cos(angle);
                const crown = Math.sin(angle);
                const shoulder = Math.pow(crown, s.power || 0.58);
                const z = side * s.halfWidth;
                const y = bottomY + shoulder * (s.height - bottomY);
                vertices.push(s.x, y, z);

                const normal = new THREE.Vector3(0, shoulder * 1.25, side * 0.8).normalize();
                normals.push(normal.x, normal.y, normal.z);
            }
        });

        const stride = radialSegments + 1;
        for (let i = 0; i < stations.length - 1; i++) {
            for (let j = 0; j < radialSegments; j++) {
                const a = i * stride + j;
                const b = (i + 1) * stride + j;
                const c = (i + 1) * stride + j + 1;
                const d = i * stride + j + 1;
                indices.push(a, b, d, b, c, d);
            }
        }

        for (let j = 0; j < radialSegments; j++) {
            indices.push(0, j + 1, j);
            const last = (stations.length - 1) * stride;
            indices.push(last, last + j, last + j + 1);
        }

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.Float32BufferAttribute(vertices, 3));
        geometry.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
        geometry.setIndex(indices);
        geometry.computeVertexNormals();
        return geometry;
    }

    function buildSmoothBodyShell() {
        // Model Y-like one-piece egg-shaped crossover body: short nose, high roof, sloped hatch.
        const lowerBodyStations = [
            { x: -2.55, halfWidth: 0.38, height: 0.72, power: 0.8 },
            { x: -2.25, halfWidth: 0.82, height: 1.05, power: 0.62 },
            { x: -1.55, halfWidth: 1.03, height: 1.22, power: 0.54 },
            { x: -0.55, halfWidth: 1.08, height: 1.28, power: 0.5 },
            { x: 0.55, halfWidth: 1.05, height: 1.23, power: 0.52 },
            { x: 1.45, halfWidth: 0.97, height: 1.07, power: 0.6 },
            { x: 2.2, halfWidth: 0.78, height: 0.88, power: 0.75 },
            { x: 2.48, halfWidth: 0.34, height: 0.62, power: 0.9 }
        ];

        const body = new THREE.Mesh(createLoftGeometry(lowerBodyStations, 40, 0.26), bodyMaterial);
        body.castShadow = true;
        body.receiveShadow = true;
        bodyGroup.add(body);

        const hoodShape = new THREE.Shape();
        hoodShape.moveTo(0.8, 1.18);
        hoodShape.bezierCurveTo(1.2, 1.15, 1.8, 1.02, 2.28, 0.78);
        hoodShape.quadraticCurveTo(2.04, 0.72, 1.54, 0.73);
        hoodShape.bezierCurveTo(1.0, 0.78, 0.68, 0.93, 0.8, 1.18);
        addSideShape(hoodShape, 0.015, 0x000000, 0.18, 0.02);

        addCharacterLine(1, [
            [-2.05, 1.03, 1.02],
            [-1.1, 1.12, 1.1],
            [0.25, 1.13, 1.1],
            [1.35, 1.0, 0.98],
            [2.05, 0.76, 0.7]
        ], bodyMaterial, 0.018);
        addCharacterLine(-1, [
            [-2.05, 1.03, 1.02],
            [-1.1, 1.12, 1.1],
            [0.25, 1.13, 1.1],
            [1.35, 1.0, 0.98],
            [2.05, 0.76, 0.7]
        ], bodyMaterial, 0.018);

        const rockerMat = new THREE.MeshStandardMaterial({ color: 0x0b0b0d, roughness: 0.65, metalness: 0.25 });
        addCharacterLine(1, [
            [-2.1, 0.42, 0.98],
            [-0.7, 0.38, 1.08],
            [0.9, 0.4, 1.02],
            [2.0, 0.44, 0.75]
        ], rockerMat, 0.04);
        addCharacterLine(-1, [
            [-2.1, 0.42, 0.98],
            [-0.7, 0.38, 1.08],
            [0.9, 0.4, 1.02],
            [2.0, 0.44, 0.75]
        ], rockerMat, 0.04);
    }

    function buildGlassCanopy() {
        const glassMaterial = new THREE.MeshPhysicalMaterial({
            color: 0x07090f,
            roughness: 0.03,
            metalness: 0,
            transmission: 0.45,
            transparent: true,
            opacity: 0.58,
            clearcoat: 1,
            clearcoatRoughness: 0.02
        });

        const canopyStations = [
            { x: -1.75, halfWidth: 0.58, height: 1.2, power: 0.95 },
            { x: -1.35, halfWidth: 0.73, height: 1.62, power: 0.56 },
            { x: -0.65, halfWidth: 0.8, height: 1.86, power: 0.48 },
            { x: 0.15, halfWidth: 0.78, height: 1.9, power: 0.48 },
            { x: 0.85, halfWidth: 0.68, height: 1.62, power: 0.55 },
            { x: 1.36, halfWidth: 0.48, height: 1.18, power: 0.8 }
        ];

        const canopy = new THREE.Mesh(createLoftGeometry(canopyStations, 40, 1.08), glassMaterial);
        canopy.castShadow = true;
        bodyGroup.add(canopy);

        const roofFrame = new THREE.Mesh(
            createLoftGeometry([
                { x: -1.86, halfWidth: 0.62, height: 1.21, power: 0.95 },
                { x: -1.38, halfWidth: 0.78, height: 1.68, power: 0.58 },
                { x: -0.45, halfWidth: 0.86, height: 1.96, power: 0.5 },
                { x: 0.55, halfWidth: 0.78, height: 1.75, power: 0.55 },
                { x: 1.46, halfWidth: 0.54, height: 1.2, power: 0.9 }
            ], 40, 1.05),
            new THREE.MeshPhysicalMaterial({
                color: new THREE.Color(config.exteriorColor),
                ...paintOptions,
                transparent: true,
                opacity: 0.24
            })
        );
        roofFrame.castShadow = true;
        bodyGroup.add(roofFrame);
    }

    function buildSideWindows() {
        const sideGlass = new THREE.MeshPhysicalMaterial({
            color: 0x05070d,
            roughness: 0.03,
            metalness: 0,
            transparent: true,
            opacity: 0.72,
            clearcoat: 1
        });

        const windowShape = new THREE.Shape();
        windowShape.moveTo(-1.78, 1.2);
        windowShape.bezierCurveTo(-1.48, 1.62, -0.72, 1.76, 0.05, 1.76);
        windowShape.bezierCurveTo(0.72, 1.74, 1.2, 1.48, 1.42, 1.18);
        windowShape.quadraticCurveTo(0.35, 1.12, -1.78, 1.2);

        addSideShape(windowShape, 1.035, sideGlass, 1);
        addSideShape(windowShape, -1.035, sideGlass, -1);

        const blackTrim = new THREE.MeshStandardMaterial({ color: 0x060608, roughness: 0.35, metalness: 0.3 });
        addCharacterLine(1, [
            [-1.8, 1.18, 1.05],
            [-0.75, 1.15, 1.08],
            [0.55, 1.14, 1.05],
            [1.42, 1.16, 0.86]
        ], blackTrim, 0.025);
        addCharacterLine(-1, [
            [-1.8, 1.18, 1.05],
            [-0.75, 1.15, 1.08],
            [0.55, 1.14, 1.05],
            [1.42, 1.16, 0.86]
        ], blackTrim, 0.025);
    }

    function addSideShape(shape, z, materialOrColor, sideSign, opacity) {
        const material = typeof materialOrColor === 'number'
            ? new THREE.MeshStandardMaterial({
                color: materialOrColor,
                transparent: opacity !== undefined,
                opacity: opacity === undefined ? 1 : opacity,
                roughness: 0.5
            })
            : materialOrColor;
        const mesh = new THREE.Mesh(new THREE.ShapeGeometry(shape, 32), material);
        mesh.position.z = z;
        mesh.rotation.y = sideSign < 0 ? Math.PI : 0;
        mesh.scale.z = sideSign < 0 ? -1 : 1;
        bodyGroup.add(mesh);
    }

    function addCharacterLine(sideSign, points, material, radius) {
        const curve = new THREE.CatmullRomCurve3(points.map(([x, y, z]) => new THREE.Vector3(x, y, sideSign * z)));
        const tube = new THREE.Mesh(new THREE.TubeGeometry(curve, 48, radius, 8, false), material);
        tube.castShadow = true;
        bodyGroup.add(tube);
    }

    function buildSignatureDetails() {
        const headlightMaterial = new THREE.MeshStandardMaterial({
            color: 0xffffff,
            emissive: 0xffffee,
            emissiveIntensity: 0.75,
            roughness: 0.12
        });
        [-0.52, 0.52].forEach((z) => {
            const light = new THREE.Mesh(new THREE.SphereGeometry(0.13, 24, 12), headlightMaterial);
            light.position.set(2.28, 0.86, z);
            light.scale.set(1.75, 0.38, 0.65);
            light.rotation.z = 0.12;
            bodyGroup.add(light);
        });

        const tailMaterial = new THREE.MeshStandardMaterial({
            color: 0xff1111,
            emissive: 0xff0000,
            emissiveIntensity: 0.55,
            roughness: 0.18
        });
        const tailCurve = new THREE.CatmullRomCurve3([
            new THREE.Vector3(-2.48, 0.98, -0.72),
            new THREE.Vector3(-2.55, 1.02, 0),
            new THREE.Vector3(-2.48, 0.98, 0.72)
        ]);
        bodyGroup.add(new THREE.Mesh(new THREE.TubeGeometry(tailCurve, 36, 0.025, 8, false), tailMaterial));

        const trimMat = new THREE.MeshStandardMaterial({ color: 0x08080a, roughness: 0.65, metalness: 0.35 });
        const frontFascia = new THREE.Mesh(new THREE.SphereGeometry(0.44, 32, 12), trimMat);
        frontFascia.position.set(2.28, 0.38, 0);
        frontFascia.scale.set(0.55, 0.35, 1.9);
        bodyGroup.add(frontFascia);

        const rearFascia = new THREE.Mesh(new THREE.SphereGeometry(0.46, 32, 12), trimMat);
        rearFascia.position.set(-2.28, 0.39, 0);
        rearFascia.scale.set(0.55, 0.35, 1.9);
        bodyGroup.add(rearFascia);

        [-1, 1].forEach((side) => {
            const mirrorStem = new THREE.Mesh(
                new THREE.CapsuleGeometry(0.025, 0.2, 8, 16),
                bodyMaterial
            );
            mirrorStem.position.set(1.22, 1.16, side * 1.05);
            mirrorStem.rotation.z = Math.PI / 2;
            bodyGroup.add(mirrorStem);

            const mirror = new THREE.Mesh(new THREE.SphereGeometry(0.13, 24, 16), bodyMaterial);
            mirror.position.set(1.34, 1.14, side * 1.2);
            mirror.scale.set(1.2, 0.55, 0.8);
            bodyGroup.add(mirror);
        });

        buildWheelArchTrim(trimMat);
    }

    function buildWheelArchTrim(material) {
        [
            { x: 1.33, z: 1.02 },
            { x: 1.33, z: -1.02 },
            { x: -1.33, z: 1.02 },
            { x: -1.33, z: -1.02 }
        ].forEach((pos) => {
            const trim = new THREE.Mesh(new THREE.TorusGeometry(0.49, 0.045, 12, 48, Math.PI), material);
            trim.position.set(pos.x, 0.49, pos.z);
            trim.rotation.y = pos.z > 0 ? -Math.PI / 2 : Math.PI / 2;
            trim.castShadow = true;
            bodyGroup.add(trim);
        });
    }

    function buildInterior() {
        const seatPositions = [
            [0.25, 0.46], [0.25, -0.46],
            [-0.55, 0.46], [-0.55, -0.46]
        ];
        seatPositions.forEach(([x, z]) => {
            const seat = new THREE.Group();
            const base = new THREE.Mesh(new THREE.SphereGeometry(0.22, 24, 16), interiorMaterial);
            base.scale.set(1.25, 0.38, 0.9);
            base.position.set(x, 0.76, z);
            const back = new THREE.Mesh(new THREE.SphereGeometry(0.23, 24, 16), interiorMaterial);
            back.scale.set(0.5, 1.2, 0.85);
            back.position.set(x - 0.16, 1.0, z);
            seat.add(base, back);
            interiorGroup.add(seat);
        });

        const dash = new THREE.Mesh(new THREE.SphereGeometry(0.28, 32, 12), interiorMaterial);
        dash.position.set(0.95, 0.95, 0);
        dash.scale.set(0.7, 0.35, 2.6);
        interiorGroup.add(dash);

        const screen = new THREE.Mesh(
            new THREE.BoxGeometry(0.025, 0.36, 0.54),
            new THREE.MeshStandardMaterial({ color: 0x060814, emissive: 0x1d5cff, emissiveIntensity: 0.35 })
        );
        screen.position.set(0.78, 1.1, 0);
        interiorGroup.add(screen);

        const steering = new THREE.Mesh(
            new THREE.TorusGeometry(0.13, 0.018, 12, 32),
            new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.55 })
        );
        steering.position.set(0.7, 1.02, 0.48);
        steering.rotation.y = Math.PI / 2;
        steering.rotation.x = 0.45;
        interiorGroup.add(steering);
    }

    function buildWheels(type) {
        wheelGroups.forEach((group) => scene.remove(group));
        wheelGroups = [];

        const wheelPositions = [
            { x: 1.33, z: 1.08 },
            { x: 1.33, z: -1.08 },
            { x: -1.33, z: 1.08 },
            { x: -1.33, z: -1.08 }
        ];

        const tireMaterial = new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.88 });
        const rimMaterial = new THREE.MeshStandardMaterial({
            color: type === 'gemini' ? 0x9a9a9a : 0x222222,
            metalness: 0.95,
            roughness: type === 'gemini' ? 0.32 : 0.18
        });
        const tireRadius = type === 'gemini' ? 0.46 : 0.49;
        const rimRadius = type === 'gemini' ? 0.31 : 0.36;
        const spokeCount = type === 'gemini' ? 5 : 10;

        wheelPositions.forEach((pos) => {
            const wheel = new THREE.Group();
            const outward = pos.z > 0 ? 1 : -1;

            const tire = new THREE.Mesh(new THREE.TorusGeometry(tireRadius, 0.105, 20, 48), tireMaterial);
            tire.rotation.y = Math.PI / 2;
            tire.castShadow = true;
            wheel.add(tire);

            const rim = new THREE.Mesh(new THREE.CylinderGeometry(rimRadius, rimRadius, 0.15, 48), rimMaterial);
            rim.rotation.z = Math.PI / 2;
            wheel.add(rim);

            const face = new THREE.Mesh(new THREE.CircleGeometry(rimRadius, 48), rimMaterial);
            face.position.x = outward * 0.085;
            face.rotation.y = outward * Math.PI / 2;
            wheel.add(face);

            for (let i = 0; i < spokeCount; i++) {
                const angle = (i / spokeCount) * Math.PI * 2;
                const spoke = new THREE.Mesh(
                    new THREE.CapsuleGeometry(type === 'gemini' ? 0.025 : 0.018, rimRadius * 0.82, 8, 12),
                    rimMaterial
                );
                spoke.position.x = outward * 0.1;
                spoke.position.y = Math.sin(angle) * rimRadius * 0.36;
                spoke.position.z = Math.cos(angle) * rimRadius * 0.36;
                spoke.rotation.x = angle;
                spoke.rotation.z = Math.PI / 2;
                wheel.add(spoke);
            }

            const cap = new THREE.Mesh(
                new THREE.CylinderGeometry(0.065, 0.065, 0.035, 24),
                new THREE.MeshStandardMaterial({ color: 0x101010, roughness: 0.35, metalness: 0.8 })
            );
            cap.rotation.z = Math.PI / 2;
            cap.position.x = outward * 0.11;
            wheel.add(cap);

            wheel.position.set(pos.x, 0.49, pos.z);
            scene.add(wheel);
            wheelGroups.push(wheel);
        });
    }

    function setExteriorColor(color) {
        config.exteriorColor = color;
        bodyMaterial.color.set(color);
    }

    function setWheelType(type) {
        config.wheelType = type;
        buildWheels(type);
    }

    function setInteriorColor(color) {
        config.interiorColor = color;
        interiorMaterial.color.set(color);
    }

    function setupEventListeners() {
        document.querySelectorAll('#exterior-colors .color-swatch').forEach((swatch) => {
            swatch.addEventListener('click', () => {
                document.querySelectorAll('#exterior-colors .color-swatch').forEach((s) => s.classList.remove('active'));
                swatch.classList.add('active');
                setExteriorColor(swatch.dataset.color);
            });
        });

        document.querySelectorAll('#wheel-options .wheel-option').forEach((option) => {
            option.addEventListener('click', () => {
                document.querySelectorAll('#wheel-options .wheel-option').forEach((o) => o.classList.remove('active'));
                option.classList.add('active');
                setWheelType(option.dataset.wheel);
            });
        });

        document.querySelectorAll('#interior-colors .color-swatch').forEach((swatch) => {
            swatch.addEventListener('click', () => {
                document.querySelectorAll('#interior-colors .color-swatch').forEach((s) => s.classList.remove('active'));
                swatch.classList.add('active');
                setInteriorColor(swatch.dataset.color);
            });
        });

        window.addEventListener('resize', () => {
            camera.aspect = window.innerWidth / window.innerHeight;
            camera.updateProjectionMatrix();
            renderer.setSize(window.innerWidth, window.innerHeight);
        });
    }

    function animate() {
        requestAnimationFrame(animate);
        controls.update();
        renderer.render(scene, camera);
    }

    init();
})();
